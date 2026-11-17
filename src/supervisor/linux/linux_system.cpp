// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "linux_system.hpp"

#include "probes.hpp"
#include "spawn.hpp"

#include <pixterm/netstat.hpp>
#include <pixterm/pin.hpp>
#include <pixterm/settings_store.hpp>

#include <fcntl.h>
#include <openssl/rand.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <print>
#include <ranges>
#include <thread>

namespace pixterm::supervisor {

namespace {

constexpr std::size_t tail_limit = 2048;
constexpr std::size_t job_output_limit = 4096;
constexpr std::size_t recent_log_lines = 200;
constexpr Duration helper_timeout{30'000};
constexpr Duration drain_wait{100};
constexpr std::string_view display_name = ":0";

constexpr std::array slots{Slot::xorg, Slot::mwm, Slot::chooser, Slot::master, Slot::client};

// Signals a program and its process group. Right after fork the child
// may not have made its group yet, so the program itself is signalled
// too.
void signal_group(pid_t leader, int signal) {
    ::kill(-leader, signal);
    ::kill(leader, signal);
}

void set_nonblocking(int fd) {
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
}

struct Pipe {
    UniqueFd read;
    UniqueFd write;
};

std::optional<Pipe> make_pipe() {
    std::array<int, 2> fds{};
    if (::pipe2(fds.data(), O_CLOEXEC) != 0)
        return std::nullopt;
    return Pipe{.read = UniqueFd(fds[0]), .write = UniqueFd(fds[1])};
}

// Keeps the end of a growing text: errors come last.
void append_tail(std::string& text, std::string_view more, std::size_t limit) {
    text += more;
    if (text.size() > limit)
        text.erase(0, text.size() - limit);
}

std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
        text.pop_back();
    return text;
}

// Reads what is available now from a non-blocking descriptor. Returns
// false at end of file or on an error.
bool read_available(int fd, std::string& out, std::size_t limit) {
    std::array<char, 1024> buffer{};
    for (;;) {
        const auto n = ::read(fd, buffer.data(), buffer.size());
        if (n > 0) {
            append_tail(out, std::string_view(buffer.data(), static_cast<std::size_t>(n)), limit);
            continue;
        }
        return n < 0 && (errno == EAGAIN || errno == EINTR);
    }
}

// Reads what a program that has exited wrote last, waiting briefly for
// its descendants to close the pipe.
void drain(int fd, std::string& out, std::size_t limit) {
    while (fd >= 0) {
        pollfd p{.fd = fd, .events = POLLIN, .revents = 0};
        if (::poll(&p, 1, static_cast<int>(drain_wait.count())) <= 0
            || !read_available(fd, out, limit))
            return;
    }
}

} // namespace

LinuxSystem::LinuxSystem(EventLoop& loop, LinuxPaths paths)
    : loop_(loop), paths_(std::move(paths)) {}

LinuxSystem::~LinuxSystem() = default;

// ---- the log --------------------------------------------------------

void LinuxSystem::log(std::string_view message) {
    std::println(stderr, "pixtermd: {}", message);
    // A copy for Configure > Diagnostics: the latest lines, with times.
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    std::array<char, 16> stamp{};
    if (::localtime_r(&t, &local) != nullptr)
        (void)std::strftime(stamp.data(), stamp.size(), "%H:%M:%S", &local);
    recent_log_.push_back(std::format("{} {}", stamp.data(), message.substr(0, 400)));
    if (recent_log_.size() > recent_log_lines)
        recent_log_.pop_front();
    std::string text;
    for (const auto& line : recent_log_)
        text += line + '\n';
    (void)write_file(paths_.log_file, text);
}

LoadedSettings LinuxSystem::load_settings() {
    return SettingsStore(paths_.state_dir, paths_.factory_settings).load();
}

// ---- the X server ---------------------------------------------------

// A fresh cookie each time, fed to xauth on its standard input so that
// it never appears in an argument list.
bool LinuxSystem::write_xauthority() {
    std::array<unsigned char, 16> cookie{};
    if (RAND_bytes(cookie.data(), static_cast<int>(cookie.size())) != 1)
        return false;
    std::string line = std::format("add {} . ", display_name);
    for (const auto b : cookie)
        line += std::format("{:02x}", b);
    line += '\n';
    wipe({reinterpret_cast<char*>(cookie.data()), cookie.size()});

    ::unlink(paths_.xauthority.c_str());
    const UniqueFd created(
        ::open(paths_.xauthority.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    auto input = make_pipe();
    bool ok = created && input;
    if (ok) {
        const auto pid = spawn(
            {.argv = {paths_.xauth.string(), "-q", "-f", paths_.xauthority.string(), "source", "-"},
             .env = paths_.env,
             .in = input->read.get(),
             .out = inherit_fd,
             .err = inherit_fd,
             .pass_fd = -1,
             .new_session = true});
        input->read.reset();
        ok = pid && write_all(input->write.get(), line);
        input->write.reset();
        int status = 0;
        ok = ok && ::waitpid(*pid, &status, 0) == *pid && WIFEXITED(status)
             && WEXITSTATUS(status) == 0;
    }
    wipe(line);
    if (!ok)
        log("xauth failed");
    return ok;
}

// The keyboard as an Xorg configuration file, linked from
// /etc/X11/xorg.conf.d, so that every X server starts with it: a
// session's own display must never get a client of ours, whose
// disconnecting could reset the server and end the session.
void LinuxSystem::write_keyboard_conf(const Settings& settings) {
    // The layout and variant were checked: lower-case letters, digits,
    // '_' and '-'.
    const auto text = std::format(R"(# Written by pixtermd from the terminal's settings.
Section "InputClass"
	Identifier "pixterm-keyboard"
	MatchIsKeyboard "on"
	Option "XkbLayout" "{}"
	Option "XkbVariant" "{}"
	Option "XkbOptions" "terminate:ctrl_alt_bksp"
EndSection
)",
                                  settings.keyboard_layout, settings.keyboard_variant);
    const auto tmp = paths_.keyboard_conf.string() + ".tmp";
    if (!write_file(tmp, text) || ::rename(tmp.c_str(), paths_.keyboard_conf.c_str()) != 0)
        log(std::format("cannot write {}", paths_.keyboard_conf.string()));
}

bool LinuxSystem::start_xorg(const Settings& settings, const std::vector<std::string>& extra_args) {
    write_keyboard_conf(settings);
    auto ready = make_pipe();
    if (!write_xauthority() || !ready)
        return false;
    std::vector<std::string> argv{paths_.xorg.string(),
                                  std::string(display_name),
                                  "vt1",
                                  "-keeptty",
                                  "-novtswitch",
                                  "-auth",
                                  paths_.xauthority.string(),
                                  "-logfile",
                                  paths_.xorg_log.string(),
                                  "-displayfd",
                                  std::to_string(ready->write.get())};
    argv.insert(argv.end(), extra_args.begin(), extra_args.end());
    const auto pid = spawn({.argv = std::move(argv),
                            .env = paths_.env,
                            .in = inherit_fd,
                            .out = inherit_fd,
                            .err = inherit_fd,
                            .pass_fd = ready->write.get(),
                            .new_session = false});
    if (!pid) {
        log(std::format("cannot start Xorg: {}", pid.error().message()));
        return false;
    }
    auto& c = child(Slot::xorg);
    c.name = "Xorg";
    c.pid = *pid;
    c.group = 0;
    log(std::format("started Xorg (pid {})", *pid));

    // Xorg writes its display number, then a newline, when it accepts
    // connections. They are separate writes, and Xorg treats a failed
    // second write as fatal, so the pipe stays open until the newline.
    ready->write.reset();
    display_fd_ = std::move(ready->read);
    set_nonblocking(display_fd_.get());
    display_text_.clear();
    loop_.unwatch(display_watch_);
    display_watch_ = loop_.watch(display_fd_.get(), POLLIN, [this](short) {
        const bool open = read_available(display_fd_.get(), display_text_, 64);
        if (display_text_.contains('\n')) {
            loop_.unwatch(display_watch_);
            display_fd_.reset();
            events_->x_ready();
        } else if (!open) {
            // Xorg died; its exit is handled when it is reaped.
            loop_.unwatch(display_watch_);
            display_fd_.reset();
        }
    });
    return true;
}

// ---- programs -------------------------------------------------------

void LinuxSystem::capture(Child& c, int read_end) {
    set_nonblocking(read_end);
    c.output.reset(read_end);
    c.tail.clear();
    c.output_watch = loop_.watch(read_end, POLLIN, [this, &c](short) { read_output(c); });
}

void LinuxSystem::read_output(Child& c) {
    std::string fresh;
    const bool open = read_available(c.output.get(), fresh, tail_limit);
    if (!fresh.empty()) {
        // Into the journal as well, a line at a time.
        for (const auto line : std::views::split(trimmed(fresh), '\n'))
            std::println(stderr, "{}: {}", c.name, std::string_view(line));
        append_tail(c.tail, fresh, tail_limit);
    }
    if (!open) {
        loop_.unwatch(c.output_watch);
        c.output.reset();
    }
}

bool LinuxSystem::start(Slot slot, Program program) {
    auto output = make_pipe();
    auto input = make_pipe();
    if (!output || !input)
        return false;
    const auto pid = spawn({.argv = program.argv,
                            .env = program.env,
                            .in = input->read.get(),
                            .out = output->write.get(),
                            .err = output->write.get(),
                            .pass_fd = -1,
                            .new_session = true});
    if (!pid) {
        log(std::format("cannot start {}: {}", program.name, pid.error().message()));
        wipe(program.input);
        return false;
    }
    auto& c = child(slot);
    c.name = program.name;
    c.pid = *pid;
    c.group = *pid;
    capture(c, output->read.release());
    input->read.reset();
    // Input is small (a password), and a pipe holds far more, so this
    // cannot block.
    if (!program.input.empty() && !write_all(input->write.get(), program.input))
        log(std::format("cannot pass input to {}", program.name));
    wipe(program.input);
    if (program.hold_input)
        c.input = std::move(input->write);
    log(std::format("started {} (pid {})", program.name, *pid));
    return true;
}

bool LinuxSystem::start_chooser(Program program) {
    std::array<int, 2> pair{};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair.data()) != 0)
        return false;
    UniqueFd ours(pair[0]);
    const UniqueFd theirs(pair[1]);
    auto output = make_pipe();
    if (!output)
        return false;
    program.env.push_back(std::format("{}={}", control_fd_variable, theirs.get()));
    const auto pid = spawn({.argv = program.argv,
                            .env = program.env,
                            .in = -1,
                            .out = output->write.get(),
                            .err = output->write.get(),
                            .pass_fd = theirs.get(),
                            .new_session = true});
    if (!pid) {
        log(std::format("cannot start the chooser: {}", pid.error().message()));
        return false;
    }
    auto& c = child(Slot::chooser);
    c.name = program.name;
    c.pid = *pid;
    c.group = *pid;
    capture(c, output->read.release());
    log(std::format("started {} (pid {})", program.name, *pid));

    close_chooser();
    chooser_ = std::move(ours);
    set_nonblocking(chooser_.get());
    chooser_lines_ = {};
    chooser_watch_ = loop_.watch(chooser_.get(), POLLIN, [this](short) {
        std::string data;
        const bool open = read_available(chooser_.get(), data, std::size_t{64} * 1024);
        for (const auto& line : chooser_lines_.feed(data))
            events_->chooser_line(line);
        if (!open)
            close_chooser();
    });
    return true;
}

bool LinuxSystem::running(Slot slot) const {
    return child(slot).pid > 0;
}

void LinuxSystem::signal(Slot slot, int signal) {
    const auto& c = child(slot);
    if (c.pid <= 0)
        return;
    if (c.group > 0)
        signal_group(c.group, signal);
    else
        ::kill(c.pid, signal);
}

void LinuxSystem::close_input(Slot slot) {
    child(slot).input.reset();
}

// Catches descendants that outlived the program that started them.
void LinuxSystem::kill_leftovers() {
    for (auto& c : children_) {
        if (c.group > 0)
            ::kill(-c.group, SIGKILL);
        c.group = 0;
    }
}

void LinuxSystem::send_to_chooser(std::string_view line) {
    if (chooser_)
        (void)send_line(chooser_.get(), line);
}

void LinuxSystem::close_chooser() {
    loop_.unwatch(chooser_watch_);
    chooser_.reset();
}

void LinuxSystem::child_reaped(Slot slot, int status) {
    auto& c = child(slot);
    c.pid = 0;
    if (c.output) {
        std::string rest;
        drain(c.output.get(), rest, tail_limit);
        append_tail(c.tail, rest, tail_limit);
        loop_.unwatch(c.output_watch);
        c.output.reset();
    }
    c.input.reset();
    if (slot == Slot::chooser)
        close_chooser();
    const auto tail = trimmed(std::exchange(c.tail, {}));
    events_->child_exited(slot, ExitStatus::from_wait(status), tail);
}

void LinuxSystem::reap() {
    int status = 0;
    // Xorg first: when the display dies, the session exits too, and the
    // user should hear of the display reset, not of a failed session.
    auto& xorg = child(Slot::xorg);
    if (xorg.pid > 0 && ::waitpid(xorg.pid, &status, WNOHANG) == xorg.pid)
        child_reaped(Slot::xorg, status);
    pid_t pid = 0;
    while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == job_.pid) {
            job_reaped(status);
            continue;
        }
        for (const auto slot : slots) {
            if (child(slot).pid == pid) {
                child_reaped(slot, status);
                break;
            }
        }
    }
}

void LinuxSystem::terminate_all() {
    cancel_job();
    for (const auto slot : slots)
        signal(slot, SIGTERM);
    const auto until = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < until
           && std::ranges::any_of(slots, [this](Slot s) { return running(s); })) {
        int status = 0;
        const pid_t pid = ::waitpid(-1, &status, WNOHANG);
        for (auto& c : children_)
            if (pid > 0 && c.pid == pid)
                c.pid = 0;
        if (pid <= 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (const auto slot : slots)
        signal(slot, SIGKILL);
    kill_leftovers();
}

// ---- jobs -----------------------------------------------------------

bool LinuxSystem::start_job(Job job, JobDone done) {
    if (job_running())
        return false;
    auto output = make_pipe();
    if (!output)
        return false;
    std::expected<pid_t, std::error_code> pid;
    if (auto* program = std::get_if<Program>(&job.task)) {
        auto input = make_pipe();
        if (!input)
            return false;
        pid = spawn({.argv = program->argv,
                     .env = program->env,
                     .in = input->read.get(),
                     .out = output->write.get(),
                     .err = output->write.get(),
                     .pass_fd = -1,
                     .new_session = true});
        input->read.reset();
        if (pid && !program->input.empty())
            (void)write_all(input->write.get(), program->input);
        wipe(program->input);
    } else if (const auto* tcp = std::get_if<TcpProbe>(&job.task)) {
        pid = fork_task([tcp] { return run_probe(*tcp); }, output->write.get());
    } else if (const auto* x = std::get_if<XdmcpProbe>(&job.task)) {
        pid = fork_task([x] { return run_probe(*x); }, output->write.get());
    }
    if (!pid) {
        log(std::format("cannot start {}: {}", job.name, pid.error().message()));
        return false;
    }
    output->write.reset();
    set_nonblocking(output->read.get());
    job_ = RunningJob{.name = job.name,
                      .pid = *pid,
                      .output = std::move(output->read),
                      .output_watch = -1,
                      .text = {},
                      .deadline = now() + job.timeout,
                      .secret = job.secret,
                      .done = std::move(done)};
    job_.output_watch = loop_.watch(job_.output.get(), POLLIN, [this](short) {
        if (!read_available(job_.output.get(), job_.text, job_output_limit)) {
            loop_.unwatch(job_.output_watch);
            job_.output.reset();
        }
    });
    return true;
}

void LinuxSystem::cancel_job() {
    if (!job_running())
        return;
    signal_group(job_.pid, SIGKILL);
    job_.done = nullptr;
}

void LinuxSystem::job_reaped(int status) {
    if (job_.output) {
        drain(job_.output.get(), job_.text, job_output_limit);
        loop_.unwatch(job_.output_watch);
        job_.output.reset();
    }
    auto finished = std::exchange(job_, RunningJob{});
    auto text = trimmed(std::move(finished.text));
    if (!finished.secret && !text.empty() && !finished.name.starts_with("xdmcp"))
        log(std::format("{}: {}", finished.name, text));
    if (finished.done)
        finished.done(ExitStatus::from_wait(status), text);
    if (finished.secret)
        wipe(text);
}

// ---- the helper -----------------------------------------------------

bool LinuxSystem::call_helper(helper::Request request, HelperDone done) {
    if (helper_busy())
        return false;
    auto bytes = helper::format_request(request);
    helper_ = HelperCall{};
    helper_.done = std::move(done);
    helper_.deadline = now() + helper_timeout;
    if (!bytes) {
        helper_.socket.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
        helper_finish("The request could not be made: " + bytes.error());
        return true;
    }
    log(std::format("helper request: {}", request.args.front()));
    helper_.request = std::move(*bytes);
    helper_.socket.reset(::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    paths_.helper_socket.string().copy(address.sun_path, sizeof address.sun_path - 1);
    if (!helper_.socket
        || ::connect(helper_.socket.get(), reinterpret_cast<const sockaddr*>(&address),
                     sizeof address)
               != 0) {
        const auto why = last_error().message();
        if (!helper_.socket)
            helper_.socket.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
        helper_finish(std::format("The privileged helper is not available ({}).", why));
        return true;
    }
    helper_.watch =
        loop_.watch(helper_.socket.get(), POLLIN | POLLOUT, [this](short r) { helper_io(r); });
    return true;
}

// Write the request, close our side for writing, then read the reply
// until the helper closes the connection.
void LinuxSystem::helper_io(short revents) {
    if (helper_.sent < helper_.request.size() && (revents & POLLOUT) != 0) {
        const auto n = ::write(helper_.socket.get(), helper_.request.data() + helper_.sent,
                               helper_.request.size() - helper_.sent);
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            helper_finish("The privileged helper closed the connection.");
            return;
        }
        if (n > 0) {
            helper_.sent += static_cast<std::size_t>(n);
            if (helper_.sent == helper_.request.size()) {
                ::shutdown(helper_.socket.get(), SHUT_WR);
                loop_.set_events(helper_.watch, POLLIN);
            }
        }
    }
    if ((revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        const bool open = read_available(helper_.socket.get(), helper_.reply,
                                         helper::max_line_length + helper::max_payload + 1);
        if (!open)
            helper_finish(std::nullopt);
    }
}

void LinuxSystem::helper_finish(std::optional<std::string> transport_error) {
    helper::Reply reply{.ok = false, .message = {}, .payload = {}};
    if (transport_error) {
        reply.message = std::move(*transport_error);
    } else if (auto parsed = helper::parse_reply(helper_.reply)) {
        reply = std::move(*parsed);
    } else {
        reply.message = "The privileged helper sent a reply that makes no sense.";
    }
    loop_.unwatch(helper_.watch);
    auto done = std::exchange(helper_, HelperCall{}).done;
    if (!reply.ok)
        log(std::format("helper: {}", reply.message));
    if (done)
        done(reply);
}

// ---- time -----------------------------------------------------------

std::optional<Time> LinuxSystem::next_deadline() const {
    std::optional<Time> next;
    if (job_running())
        next = job_.deadline;
    if (helper_busy() && (!next || helper_.deadline < *next))
        next = helper_.deadline;
    return next;
}

void LinuxSystem::tick() {
    const auto t = now();
    if (job_running() && t >= job_.deadline) {
        log(std::format("{} took too long", job_.name));
        job_.deadline = Time::max();
        signal_group(job_.pid, SIGKILL);
    }
    if (helper_busy() && t >= helper_.deadline)
        helper_finish("The privileged helper did not answer in time.");
}

// ---- files and the network ------------------------------------------

bool LinuxSystem::is_socket(const std::filesystem::path& path) const {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

std::error_code LinuxSystem::can_open(const std::filesystem::path& path) const {
    if (::access(path.c_str(), R_OK | W_OK) != 0)
        return last_error();
    return {};
}

std::optional<std::string> LinuxSystem::read_file(const std::filesystem::path& path) const {
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd)
        return std::nullopt;
    auto text = read_all(fd.get(), helper::max_payload);
    if (!text)
        return std::nullopt;
    return std::move(*text);
}

bool LinuxSystem::write_file(const std::filesystem::path& path, std::string_view data) {
    const UniqueFd fd(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
    return fd && write_all(fd.get(), data).has_value();
}

void LinuxSystem::remove_file(const std::filesystem::path& path) {
    ::unlink(path.c_str());
}

std::optional<bool> LinuxSystem::ethernet_carrier() const {
    return pixterm::ethernet_carrier();
}

std::optional<int> LinuxSystem::display_clients(bool tcp_only) const {
    return tcp_only ? display_tcp_connections(0) : display_connections(0);
}

} // namespace pixterm::supervisor
