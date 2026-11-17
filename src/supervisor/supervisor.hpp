// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// pixtermd's state machine: the terminal's whole graphical life.
///
///     x_start      start Xorg with a fresh authorization cookie
///     x_setup      pixterm-xsetup applies the display mode and keyboard
///     local        mwm and the chooser (pixterm-ui)
///     probe,       the session checks the host or logs in while the
///     connecting   chooser is still up, so errors and prompts appear there
///     handoff      the chooser and mwm are told to exit and are reaped,
///                  so the host's window manager never meets mwm
///     session      the session runs on the local display
///     x_switch,    or the session gets a display of its own: Xorg
///     remote       restarts with its arguments, and its exit ends the
///                  session
///     teardown     every program ends and Xorg restarts, so no X state
///                  (windows, clipboard, cookies) outlives a session
///
/// What differs between kinds of host lives in Session subclasses. The
/// supervisor reacts to events (the methods under "Events") and acts
/// only through its System, so tests can drive it with a fake system
/// and a fake clock.

#pragma once

#include "system.hpp"

#include <pixterm/settings.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::supervisor {

enum class State {
    x_start,
    x_setup,
    local,
    probe,
    connecting,
    handoff,
    session,
    teardown,
    x_switch,
    remote,
};

[[nodiscard]] std::string_view state_name(State s) noexcept;

/// Where pixtermd keeps its files and finds its programs, and the
/// environment every program it starts gets.
struct Environment {
    std::filesystem::path runtime_dir; ///< emptied at every boot
    std::filesystem::path state_dir;   ///< the settings partition
    std::filesystem::path libexec_dir; ///< pixterm's own programs
    std::vector<std::string> base_env; ///< NAME=value for every child
};

/// How long the supervisor waits for things.
namespace timeouts {
inline constexpr Duration x_start{20'000};
inline constexpr Duration handoff{3'000};
inline constexpr Duration terminate{3'000};
inline constexpr Duration restart_delay{1'000};
inline constexpr Duration restart_poll{200};
inline constexpr Duration quick_failure{10'000};
inline constexpr Duration watch_interval{1'000};
inline constexpr Duration link_loss{10'000};
inline constexpr Duration display_reset_grace{1'000};
inline constexpr Duration xsetup{5'000};
} // namespace timeouts

/// A program that fails this many times in a row, each soon after it
/// started, makes the supervisor reset the display.
inline constexpr int quick_failure_limit = 5;

class Supervisor;
class SettingsRequests;

/// What one kind of host does differently. The supervisor calls these
/// at fixed points; the defaults do nothing.
class Session {
public:
    explicit Session(Supervisor& supervisor) : sup_(supervisor) {}
    virtual ~Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /// local: the user picked the host. Start connecting (probe or
    /// connecting) and return true, or report why not and return false.
    /// username is what the chooser sent: "-" for the host's own.
    [[nodiscard]] virtual bool begin(std::string_view username) = 0;
    /// probe or connecting: the user cancelled, or the chooser died.
    virtual void cancel() {}
    /// handoff: the chooser and mwm have gone. Run the session, or call
    /// Supervisor::switch_display for a display of its own.
    virtual void start() = 0;
    /// Arguments for the session's own X server.
    [[nodiscard]] virtual std::vector<std::string> xorg_args() const { return {}; }
    /// The session's own X server accepts connections.
    virtual void display_ready() {}
    /// The session's own X server exited; explain why, if it matters.
    virtual void xorg_exited(ExitStatus /*status*/) {}
    /// A master or client program exited.
    virtual void child_exited(Slot /*slot*/, ExitStatus /*status*/, std::string_view /*output*/) {}
    /// Once a second in session and remote.
    virtual void watch(Time /*now*/) {}
    /// When the session next wants tick called.
    [[nodiscard]] virtual std::optional<Time> deadline() const { return std::nullopt; }
    virtual void tick(Time /*now*/) {}
    /// Teardown has begun.
    virtual void stop() {}
    /// Every program has exited.
    virtual void cleanup() {}

protected:
    [[nodiscard]] Supervisor& sup() const { return sup_; }

private:
    Supervisor& sup_;
};

/// The session for a host's protocol.
[[nodiscard]] std::unique_ptr<Session> make_session(Supervisor& supervisor, const Host& host);

class Supervisor {
public:
    Supervisor(System& system, Environment environment);
    ~Supervisor();
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;
    Supervisor(Supervisor&&) = delete;
    Supervisor& operator=(Supervisor&&) = delete;

    // Events, called by the system or the main loop.

    /// Starts the display. Called once.
    void start();
    /// The X server accepts connections.
    void x_ready();
    /// A program in a slot exited; output is the end of what it wrote.
    void child_exited(Slot slot, ExitStatus status, std::string_view output);
    /// The chooser sent a line.
    void chooser_line(std::string_view line);
    /// Called after anything happens, and at next_deadline: acts on
    /// whatever is due.
    void tick();
    /// When tick must next be called, if at all.
    [[nodiscard]] std::optional<Time> next_deadline() const;

    // What sessions use.

    [[nodiscard]] System& system() const { return system_; }
    [[nodiscard]] const Environment& environment() const { return env_; }
    [[nodiscard]] const Settings& settings() const { return settings_; }
    /// The host of the current connection.
    [[nodiscard]] const Host& host() const { return host_; }
    [[nodiscard]] State state() const { return state_; }
    void set_state(State s);
    void log(std::string_view message) const { system_.log(message); }

    /// A pixterm program's full path.
    [[nodiscard]] std::string libexec(std::string_view program) const;
    /// The environment for a child: the base, plus extra.
    [[nodiscard]] std::vector<std::string> child_env(std::vector<std::string> extra = {}) const;
    /// When the program in a slot last started.
    [[nodiscard]] Time started(Slot slot) const;

    /// The host accepted: close the chooser and mwm, then call
    /// Session::start.
    void start_handoff();
    /// Replace the local X server with one for the session.
    void switch_display();
    /// The session has the display: watch the link and the session.
    void session_running(State s);
    /// End everything and restart the X server.
    void teardown();

    /// Shows an error in the chooser now if it is up, else when it
    /// next starts.
    void report_error(std::string text);
    /// Tells the chooser the connection attempt ended.
    void chooser_idle();
    /// Shows a message alone on the screen, in the chooser's slot.
    void start_notice(std::string_view text);
    /// Runs pixterm-xsetup with arguments as the job.
    [[nodiscard]] bool run_xsetup(std::vector<std::string> args, JobDone done);

    /// A session program ended with an error. If the X server is
    /// exiting too, the user is told of the display reset instead;
    /// otherwise report runs after a short wait.
    void unless_display_reset(std::function<void()> report);

private:
    struct Child {
        Time started{};
        int quick_failures = 0;
    };

    System& system_;
    Environment env_;
    Settings settings_;
    Host host_;
    State state_ = State::x_start;
    std::unique_ptr<Session> session_;
    std::unique_ptr<SettingsRequests> requests_;
    bool own_display_ = false; ///< the X server is the session's own
    std::array<Child, slot_count> children_{};
    int xstart_failures_ = 0;
    std::string pending_error_;

    std::optional<Time> deadline_;   ///< the current state's timer
    std::optional<Time> restart_at_; ///< restart mwm and the chooser
    std::optional<Time> watch_at_;
    std::optional<Time> link_down_since_;
    std::optional<Time> display_check_at_;
    std::function<void()> display_check_report_;

    friend class SettingsRequests;

    void reload_settings();
    void start_x();
    void xsetup_done(ExitStatus status);
    void start_mwm();
    void start_chooser();
    void begin_session(const Host& host, std::string_view username);
    void handoff_check();
    void teardown_check();
    void watch_session(Time now);
    void xorg_exited(ExitStatus status);
    void local_exited(Slot slot, int failures);
    int note_exit(Slot slot, ExitStatus status);
    void state_timeout(Time now);
    void restart_local(Time now);
    void start_request(std::string_view id, std::string_view username);
    void query_request(std::string_view address, std::string_view name);
    [[nodiscard]] bool chooser_shown() const;
};

} // namespace pixterm::supervisor
