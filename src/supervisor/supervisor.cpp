// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "supervisor.hpp"

#include "failure_console.hpp"
#include "settings_requests.hpp"

#include <pixterm/lines.hpp>

#include <sys/wait.h>

#include <charconv>
#include <csignal>
#include <format>
#include <string>
#include <vector>

namespace pixterm::supervisor {

namespace {

constexpr std::array state_names{
    "x_start", "x_setup", "local",    "probe",    "connecting",
    "handoff", "session", "teardown", "x_switch", "remote",
};

constexpr std::array slot_names{"Xorg", "mwm", "chooser", "master", "client"};

constexpr std::string_view mwm_path = "/usr/bin/mwm";

std::size_t index(Slot slot) noexcept {
    return static_cast<std::size_t>(slot);
}

void earliest(std::optional<Time>& a, const std::optional<Time>& b) {
    if (b && (!a || *b < *a))
        a = b;
}

} // namespace

std::string_view state_name(State s) noexcept {
    return state_names.at(static_cast<std::size_t>(s));
}

ExitStatus ExitStatus::from_wait(int status) noexcept {
    if (WIFSIGNALED(status))
        return killed(WTERMSIG(status));
    return exited(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
}

Supervisor::Supervisor(System& system, Environment environment)
    : system_(system), env_(std::move(environment)),
      requests_(std::make_unique<SettingsRequests>(*this)),
      console_(std::make_unique<FailureConsole>(*this)) {}

Supervisor::~Supervisor() = default;

void Supervisor::set_state(State s) {
    if (s != state_)
        log(std::format("state {} -> {}", state_name(state_), state_name(s)));
    state_ = s;
    deadline_.reset();
}

std::string Supervisor::libexec(std::string_view program) const {
    return (env_.libexec_dir / program).string();
}

std::vector<std::string> Supervisor::child_env(std::vector<std::string> extra) const {
    auto env = env_.base_env;
    env.push_back("XENVIRONMENT="
                  + (env_.data_dir / (large_screen_ ? "Xresources-large" : "Xresources")).string());
    env.insert(env.end(), std::make_move_iterator(extra.begin()),
               std::make_move_iterator(extra.end()));
    return env;
}

Time Supervisor::started(Slot slot) const {
    return children_.at(index(slot)).started;
}

void Supervisor::reload_settings() {
    auto loaded = system_.load_settings();
    if (!loaded.problem.empty())
        log(std::format("settings: {}", loaded.problem));
    settings_ = std::move(loaded.settings);
}

// ---- the display ----------------------------------------------------

void Supervisor::start() {
    start_x();
}

void Supervisor::start_x() {
    set_state(State::x_start);
    reload_settings();
    auto args = own_display_ && session_ ? session_->xorg_args()
                                         : std::vector<std::string>{"-nolisten", "tcp", "-noreset"};
    // The screen saver blanks the screen after the idle time, and DPMS
    // then turns the monitor off; Xorg gives DPMS the same time.
    if (settings_.blank_minutes > 0)
        args.insert(args.end(), {"-s", std::to_string(settings_.blank_minutes)});
    else
        args.insert(args.end(), {"-s", "0", "-dpms"});
    if (!system_.start_xorg(settings_, args)) {
        ++xstart_failures_;
        teardown();
        return;
    }
    children_.at(index(Slot::xorg)).started = system_.now();
    deadline_ = system_.now() + timeouts::x_start;
}

void Supervisor::x_ready() {
    if (state_ != State::x_start)
        return;
    log("X server ready");
    xstart_failures_ = 0;
    console_->x_ready();
    set_state(State::x_setup);
    // Not on the session's own display: its display manager may already
    // own it, and a client of ours disconnecting could reset the server
    // and end the session. That display's keyboard comes from the Xorg
    // configuration the system writes.
    if (own_display_
        || !run_xsetup({}, [this](ExitStatus s, std::string_view out) { xsetup_done(s, out); }))
        xsetup_done(ExitStatus::exited(0), "");
}

void Supervisor::xsetup_done(ExitStatus status, std::string_view output) {
    if (state_ != State::x_setup)
        return;
    if (!status.succeeded())
        log("pixterm-xsetup failed");
    // "SCREEN width height", after any error lines.
    constexpr std::string_view screen = "SCREEN ";
    if (const auto at = output.find(screen); at != std::string_view::npos) {
        const auto size = output.substr(at + screen.size());
        const auto space = size.find(' ');
        int height = 0;
        if (space != std::string_view::npos) {
            auto h = size.substr(space + 1);
            h = h.substr(0, h.find('\n'));
            std::from_chars(h.data(), h.data() + h.size(), height);
        }
        large_screen_ = height >= large_screen_height;
    }
    if (own_display_ && session_) {
        session_->display_ready();
        return;
    }
    set_state(State::local);
    restart_at_.reset();
    start_mwm();
    start_chooser();
}

void Supervisor::switch_display() {
    set_state(State::x_switch);
    own_display_ = true;
    system_.signal(Slot::xorg, SIGTERM);
    deadline_ = system_.now() + timeouts::terminate;
}

bool Supervisor::run_xsetup(std::vector<std::string> args, JobDone done) {
    Program p{.name = "pixterm-xsetup",
              .argv = {libexec("pixterm-xsetup")},
              .env = child_env(),
              .input = {},
              .hold_input = false};
    p.argv.insert(p.argv.end(), args.begin(), args.end());
    return system_.start_job({.name = "pixterm-xsetup",
                              .task = std::move(p),
                              .timeout = timeouts::xsetup,
                              .secret = false},
                             std::move(done));
}

// ---- the local display ----------------------------------------------

void Supervisor::start_mwm() {
    if (system_.start(Slot::mwm, {.name = "mwm",
                                  .argv = {std::string(mwm_path)},
                                  .env = child_env(),
                                  .input = {},
                                  .hold_input = false}))
        children_.at(index(Slot::mwm)).started = system_.now();
}

void Supervisor::start_chooser() {
    if (!system_.start_chooser({.name = "pixterm-ui",
                                .argv = {libexec("pixterm-ui")},
                                .env = child_env(),
                                .input = {},
                                .hold_input = false}))
        return;
    children_.at(index(Slot::chooser)).started = system_.now();
    if (!pending_error_.empty()) {
        system_.send_to_chooser("ERROR " + escape_line(pending_error_));
        pending_error_.clear();
    }
}

void Supervisor::start_notice(std::string_view text) {
    if (system_.start(Slot::chooser,
                      {.name = "pixterm-notice",
                       .argv = {libexec("pixterm-ui"), "--notice", std::string(text)},
                       .env = child_env(),
                       .input = {},
                       .hold_input = false}))
        children_.at(index(Slot::chooser)).started = system_.now();
}

bool Supervisor::chooser_shown() const {
    return state_ == State::local || state_ == State::probe || state_ == State::connecting;
}

void Supervisor::report_error(std::string text) {
    log(std::format("error: {}", text));
    if (system_.chooser_connected() && chooser_shown())
        system_.send_to_chooser("ERROR " + escape_line(text));
    else
        pending_error_ = std::move(text);
}

void Supervisor::chooser_idle() {
    system_.send_to_chooser("IDLE");
}

// ---- the chooser's requests -----------------------------------------

void Supervisor::chooser_line(std::string_view line) {
    const auto words = split_words(line);
    if (words.empty())
        return;
    const auto verb = words.front();
    const bool idle = state_ == State::local && !requests_->busy();

    if (verb == "START" && idle && words.size() == 3) {
        start_request(words[1], words[2]);
    } else if (verb == "QUERY" && idle && words.size() >= 3) {
        // The name is escaped free text, and may hold spaces.
        const auto name = line.substr(line.find(words[2], verb.size() + words[1].size() + 1));
        query_request(words[1], name);
    } else if (verb == "WIFI_SCAN" && state_ == State::local && words.size() == 2) {
        requests_->scan(words[1]);
    } else if (SettingsRequests::handles(verb) && state_ == State::local) {
        requests_->request(verb);
    } else if (verb == "CANCEL" && (state_ == State::probe || state_ == State::connecting)) {
        if (session_)
            session_->cancel();
    } else {
        log(std::format("ignoring chooser request {} in state {}", verb, state_name(state_)));
        if (verb == "START" || verb == "QUERY")
            chooser_idle();
    }
}

void Supervisor::start_request(std::string_view id, std::string_view username) {
    reload_settings();
    const auto* h = settings_.find_host(id);
    if (h == nullptr) {
        report_error("That host is no longer in the list.");
        chooser_idle();
        return;
    }
    begin_session(*h, username);
}

void Supervisor::query_request(std::string_view address, std::string_view escaped_name) {
    const auto name = unescape_line(escaped_name);
    if (!is_valid_address(address) || !is_valid_name(name)) {
        report_error("That is not a usable XDMCP host.");
        chooser_idle();
        return;
    }
    // A host found by searching the network; it is not saved.
    Host h;
    h.id = "-";
    h.name = name;
    h.address = address;
    h.protocol = Protocol::xdmcp;
    h.port = default_port(Protocol::xdmcp);
    begin_session(h, "");
}

void Supervisor::begin_session(const Host& host, std::string_view username) {
    host_ = host;
    session_ = make_session(*this, host_);
    if (!session_->begin(username)) {
        session_.reset();
        chooser_idle();
    }
}

// ---- sessions -------------------------------------------------------

void Supervisor::start_handoff() {
    log(std::format("handing the display over to {}", host_.name));
    set_state(State::handoff);
    restart_at_.reset();
    system_.send_to_chooser("HANDOFF");
    system_.close_chooser();
    system_.signal(Slot::mwm, SIGTERM);
    deadline_ = system_.now() + timeouts::handoff;
    handoff_check();
}

void Supervisor::handoff_check() {
    if (state_ != State::handoff || system_.running(Slot::chooser) || system_.running(Slot::mwm)
        || system_.job_running() || !session_)
        return;
    session_->start();
}

void Supervisor::session_running(State s) {
    set_state(s);
    link_down_since_.reset();
    watch_at_ = system_.now() + timeouts::watch_interval;
}

void Supervisor::unless_display_reset(std::function<void()> report) {
    if (!system_.running(Slot::xorg)) {
        report();
        return;
    }
    display_check_report_ = std::move(report);
    display_check_at_ = system_.now() + timeouts::display_reset_grace;
}

void Supervisor::teardown() {
    if (state_ == State::teardown)
        return;
    set_state(State::teardown);
    restart_at_.reset();
    watch_at_.reset();
    display_check_at_.reset();
    display_check_report_ = nullptr;
    system_.cancel_job();
    system_.close_chooser();
    if (session_)
        session_->stop();
    for (const auto slot : {Slot::client, Slot::master, Slot::chooser, Slot::mwm, Slot::xorg})
        system_.signal(slot, SIGTERM);
    deadline_ = system_.now() + timeouts::terminate;
    teardown_check();
}

void Supervisor::teardown_check() {
    if (state_ != State::teardown || system_.job_running())
        return;
    for (const auto slot : {Slot::xorg, Slot::mwm, Slot::chooser, Slot::master, Slot::client})
        if (system_.running(slot))
            return;
    system_.kill_leftovers();
    if (session_)
        session_->cleanup();
    session_.reset();
    own_display_ = false;

    // Restart the local X server, waiting longer each time it fails.
    set_state(State::x_start);
    if (xstart_failures_ > 0) {
        const auto wait = std::chrono::seconds(1) * (1 << std::min(xstart_failures_, 5));
        log(std::format("restarting X in {} s", wait.count()));
        deadline_ = system_.now() + wait;
        console_->x_failed(xstart_failures_);
    } else {
        deadline_ = system_.now();
    }
}

void Supervisor::retry_x_now() {
    if (state_ != State::x_start || system_.running(Slot::xorg))
        return;
    log("X restart requested on the console");
    deadline_ = system_.now();
    tick();
}

void Supervisor::console_line(std::string_view line) {
    console_->line(line);
    tick();
}

void Supervisor::watch_session(Time now) {
    watch_at_ = now + timeouts::watch_interval;
    if (state_ != State::session && state_ != State::remote) {
        watch_at_.reset();
        return;
    }
    // During a session, the network link, wired or Wi-Fi, must not stay
    // down.
    if (system_.link_carrier() == false) {
        if (!link_down_since_) {
            link_down_since_ = now;
        } else if (now - *link_down_since_ >= timeouts::link_loss) {
            report_error(
                std::format("The network connection was lost (no Ethernet or Wi-Fi link).\n\n"
                            "The session on {} has ended.",
                            host_.name));
            teardown();
            return;
        }
    } else {
        link_down_since_.reset();
    }
    if (session_)
        session_->watch(now);
}

// ---- programs that exit ---------------------------------------------

int Supervisor::note_exit(Slot slot, ExitStatus status) {
    auto& c = children_.at(index(slot));
    if (status.signal != 0)
        log(std::format("{} was killed by signal {}", slot_names.at(index(slot)), status.signal));
    else
        log(std::format("{} exited with status {}", slot_names.at(index(slot)), status.code));
    const bool quick = system_.now() - c.started < timeouts::quick_failure;
    c.quick_failures = quick ? c.quick_failures + 1 : 0;
    return c.quick_failures;
}

void Supervisor::child_exited(Slot slot, ExitStatus status, std::string_view output) {
    const int failures = note_exit(slot, status);
    switch (slot) {
    case Slot::xorg:
        xorg_exited(status);
        break;
    case Slot::mwm:
    case Slot::chooser:
        local_exited(slot, failures);
        break;
    case Slot::master:
    case Slot::client:
        if (session_)
            session_->child_exited(slot, status, output);
        break;
    }
    tick();
}

void Supervisor::xorg_exited(ExitStatus status) {
    if (state_ == State::x_switch) {
        start_x();
        return;
    }
    if (own_display_
        && (state_ == State::remote || state_ == State::x_setup || state_ == State::x_start
            || state_ == State::session)) {
        if (session_)
            session_->xorg_exited(status);
    } else if (state_ == State::x_start || state_ == State::x_setup) {
        ++xstart_failures_;
        report_error("The display server failed to start.");
    } else if (state_ == State::session) {
        report_error(std::format("The session on {} was ended by a display reset.", host_.name));
    } else if (state_ != State::teardown) {
        report_error("The display server stopped unexpectedly.");
    }
    teardown();
}

void Supervisor::local_exited(Slot slot, int failures) {
    if (slot == Slot::chooser)
        system_.close_chooser();
    if (state_ == State::handoff) {
        handoff_check();
        return;
    }
    if (!chooser_shown())
        return;
    // Nobody is left to answer the session's prompts: abandon it.
    if (slot == Slot::chooser && state_ != State::local && session_)
        session_->cancel();
    if (failures >= quick_failure_limit) {
        log(std::format("{} keeps failing; resetting the display", slot_names.at(index(slot))));
        children_.at(index(slot)).quick_failures = 0;
        teardown();
    } else {
        restart_at_ = system_.now() + timeouts::restart_delay;
    }
}

// ---- time -----------------------------------------------------------

std::optional<Time> Supervisor::next_deadline() const {
    auto next = deadline_;
    earliest(next, restart_at_);
    earliest(next, watch_at_);
    earliest(next, display_check_at_);
    if (session_)
        earliest(next, session_->deadline());
    return next;
}

void Supervisor::restart_local(Time now) {
    restart_at_.reset();
    if (!system_.running(Slot::mwm))
        start_mwm();
    if (system_.running(Slot::chooser))
        return;
    if (state_ == State::local)
        start_chooser();
    else
        restart_at_ = now + timeouts::restart_poll; // once the attempt is over
}

void Supervisor::state_timeout(Time now) {
    deadline_.reset();
    switch (state_) {
    case State::x_start:
        if (!system_.running(Slot::xorg)) {
            start_x();
        } else {
            log("the X server did not become ready");
            ++xstart_failures_;
            teardown();
        }
        break;
    case State::x_switch:
        log("Xorg did not exit; killing it");
        system_.signal(Slot::xorg, SIGKILL);
        break;
    case State::handoff:
        log("the chooser or mwm did not exit; killing them");
        system_.signal(Slot::chooser, SIGKILL);
        system_.signal(Slot::mwm, SIGKILL);
        break;
    case State::teardown:
        log("programs did not exit; killing them");
        for (const auto slot : {Slot::client, Slot::master, Slot::chooser, Slot::mwm, Slot::xorg})
            system_.signal(slot, SIGKILL);
        deadline_ = now + timeouts::terminate;
        break;
    default:
        break;
    }
}

void Supervisor::tick() {
    const auto now = system_.now();
    handoff_check();
    teardown_check();
    if (display_check_at_ && now >= *display_check_at_) {
        display_check_at_.reset();
        auto report = std::exchange(display_check_report_, nullptr);
        if (report && state_ == State::session)
            report();
    }
    if (watch_at_ && now >= *watch_at_)
        watch_session(now);
    if (restart_at_ && now >= *restart_at_ && chooser_shown())
        restart_local(now);
    if (session_) {
        const auto due = session_->deadline();
        if (due && now >= *due)
            session_->tick(now);
    }
    if (deadline_ && now >= *deadline_)
        state_timeout(now);
}

} // namespace pixterm::supervisor
