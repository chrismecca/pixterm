// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include <pixterm/fd.hpp>
#include <pixterm/spawn.hpp>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <print>

namespace pixterm {

namespace {

// What a child does first: the signals pixtermd blocks or ignores go
// back to normal.
void reset_signals() {
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    for (const int s : {SIGCHLD, SIGTERM, SIGINT, SIGHUP, SIGPIPE})
        (void)std::signal(s, SIG_DFL);
}

// execve's argument lists, pointing into strings, which must outlive
// them.
std::vector<char*> pointers(std::vector<std::string>& strings) {
    std::vector<char*> p;
    p.reserve(strings.size() + 1);
    for (auto& s : strings)
        p.push_back(s.data());
    p.push_back(nullptr);
    return p;
}

} // namespace

std::expected<pid_t, std::error_code> spawn(const SpawnOptions& options) {
    if (options.argv.empty())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    // Everything that allocates happens before fork.
    auto args = options.argv;
    auto vars = options.env;
    const auto argv = pointers(args);
    const auto envp = pointers(vars);
    const UniqueFd null(::open("/dev/null", O_RDWR | O_CLOEXEC));
    if (!null)
        return std::unexpected(last_error());

    const pid_t pid = ::fork();
    if (pid < 0)
        return std::unexpected(last_error());
    if (pid > 0)
        return pid;

    reset_signals();
    if (options.new_session)
        ::setsid();
    const std::array fds{options.in, options.out, options.err};
    for (int i = 0; i < 3; ++i) {
        const int from = fds.at(static_cast<std::size_t>(i));
        if (from != inherit_fd && ::dup2(from < 0 ? null.get() : from, i) < 0)
            ::_exit(127);
    }
    if (options.pass_fd >= 0)
        ::fcntl(options.pass_fd, F_SETFD, 0);
    ::execve(argv[0], argv.data(), envp.data());
    std::println(stderr, "cannot run {}: {}", args.front(), std::strerror(errno));
    ::_exit(127);
}

int run_and_wait(const std::vector<std::string>& argv, const std::vector<std::string>& env) {
    const auto pid = spawn({.argv = argv,
                            .env = env,
                            .in = -1,
                            .out = -1,
                            .err = inherit_fd,
                            .pass_fd = -1,
                            .new_session = true});
    if (!pid)
        return -1;
    int status = 0;
    while (::waitpid(*pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::optional<std::string> run_for_output(const std::vector<std::string>& argv,
                                          const std::vector<std::string>& env, std::size_t limit) {
    std::array<int, 2> fds{};
    if (::pipe(fds.data()) != 0)
        return std::nullopt;
    const UniqueFd read_end(fds[0]);
    UniqueFd write_end(fds[1]);
    for (const int fd : fds)
        (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
    const auto pid = spawn({.argv = argv,
                            .env = env,
                            .in = -1,
                            .out = write_end.get(),
                            .err = inherit_fd,
                            .pass_fd = -1,
                            .new_session = true});
    write_end.reset();
    if (!pid)
        return std::nullopt;
    // Read to the end, keeping only limit bytes, so that the program
    // never blocks on a full pipe.
    std::string out;
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto n = ::read(read_end.get(), buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        const auto keep = std::min(static_cast<std::size_t>(n), limit - out.size());
        out.append(buffer.data(), keep);
    }
    int status = 0;
    while (::waitpid(*pid, &status, 0) < 0) {
        if (errno != EINTR)
            return std::nullopt;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return std::nullopt;
    return out;
}

std::expected<pid_t, std::error_code> fork_task(const std::function<std::string()>& task, int out) {
    const pid_t pid = ::fork();
    if (pid < 0)
        return std::unexpected(last_error());
    if (pid > 0)
        return pid;
    reset_signals();
    ::setsid();
    const auto line = task() + '\n';
    (void)write_all(out, line);
    ::_exit(0);
}

} // namespace pixterm
