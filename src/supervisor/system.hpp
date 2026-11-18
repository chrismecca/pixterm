// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Everything the supervisor asks of the operating system.
///
/// The supervisor's state machine never starts a process, opens a file
/// or reads the clock itself; it calls a System. pixtermd's LinuxSystem
/// does the work, and the unit tests use a fake that records each call
/// and lets the test answer it.

#pragma once

#include <pixterm/helper_protocol.hpp>
#include <pixterm/settings_store.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace pixterm::supervisor {

using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using Duration = std::chrono::milliseconds;

/// The long-running programs, one of each at a time.
enum class Slot {
    xorg,    ///< the X server
    mwm,     ///< the window manager under the chooser
    chooser, ///< pixterm-ui, or a notice in its place
    master,  ///< a connection that logs in before the session starts
    client,  ///< the session's own program
};

inline constexpr std::size_t slot_count = 5;

/// How a program ended.
struct ExitStatus {
    int code = 0;   ///< the exit status, if it exited
    int signal = 0; ///< the signal that killed it, or 0

    [[nodiscard]] static ExitStatus exited(int code) { return {.code = code, .signal = 0}; }
    [[nodiscard]] static ExitStatus killed(int signal) { return {.code = -1, .signal = signal}; }
    /// Converts a status from waitpid.
    [[nodiscard]] static ExitStatus from_wait(int status) noexcept;

    [[nodiscard]] bool succeeded() const noexcept { return signal == 0 && code == 0; }

    friend bool operator==(const ExitStatus&, const ExitStatus&) = default;
};

/// A program to run.
struct Program {
    std::string name;              ///< shown in the log
    std::vector<std::string> argv; ///< argv[0] is a full path
    std::vector<std::string> env;  ///< the whole environment, as NAME=value
    std::string input;             ///< written to its standard input first
    /// Keep standard input open after input is written, until
    /// close_input. Otherwise it reads end of file.
    bool hold_input = false;
};

/// Is anything listening on a TCP port? The job's output is "OK" or
/// "E <reason>".
struct TcpProbe {
    std::string address;
    int port = 0;
};

/// Does an XDMCP display manager answer? The job's output is
/// "W <host>\t<status>" (willing), "U <host>\t<status>" (unwilling),
/// "N" (no answer) or "R <reason>" (the name could not be resolved).
struct XdmcpProbe {
    std::string address;
    int port = 0;
    bool indirect = false;
};

/// A short task run in a child process: a program, or one of the
/// checks above. Its output is collected and handed to its callback.
struct Job {
    std::string name;
    std::variant<Program, TcpProbe, XdmcpProbe> task;
    Duration timeout{};
    /// The output is a secret, such as a password: it is never logged,
    /// and is wiped once the callback returns.
    bool secret = false;
};

using JobDone = std::function<void(ExitStatus status, std::string_view output)>;
using HelperDone = std::function<void(const helper::Reply& reply)>;

/// What the system reports back. The supervisor implements this.
class Events {
public:
    Events() = default;
    virtual ~Events() = default;
    Events(const Events&) = delete;
    Events& operator=(const Events&) = delete;
    Events(Events&&) = delete;
    Events& operator=(Events&&) = delete;

    /// The X server accepts connections.
    virtual void x_ready() = 0;
    /// A program in a slot exited; output is the end of what it wrote.
    virtual void child_exited(Slot slot, ExitStatus status, std::string_view output) = 0;
    /// The chooser sent a line.
    virtual void chooser_line(std::string_view line) = 0;
    /// A line was typed on the text console.
    virtual void console_line(std::string_view line) = 0;
};

class System {
public:
    System() = default;
    virtual ~System() = default;
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) = delete;
    System& operator=(System&&) = delete;

    [[nodiscard]] virtual Time now() const = 0;
    virtual void log(std::string_view message) = 0;
    [[nodiscard]] virtual LoadedSettings load_settings() = 0;

    /// Starts the X server with a fresh authorization cookie and the
    /// keyboard of settings, adding extra_args to its usual arguments.
    /// When it accepts connections, the system calls Events::x_ready.
    [[nodiscard]] virtual bool start_xorg(const Settings& settings,
                                          const std::vector<std::string>& extra_args) = 0;

    /// Starts a program in a slot, in its own process group. When it
    /// exits, the system calls Events::child_exited with the end of
    /// what it wrote.
    [[nodiscard]] virtual bool start(Slot slot, Program program) = 0;
    /// Starts the chooser in its slot with its end of the control
    /// socket. Its lines arrive through Events::chooser_line.
    [[nodiscard]] virtual bool start_chooser(Program program) = 0;
    [[nodiscard]] virtual bool running(Slot slot) const = 0;
    virtual void signal(Slot slot, int signal) = 0;
    /// Closes the standard input held open for a program.
    virtual void close_input(Slot slot) = 0;
    /// Kills whatever is left in the process groups of past programs.
    virtual void kill_leftovers() = 0;

    virtual void send_to_chooser(std::string_view line) = 0;
    virtual void close_chooser() = 0;
    [[nodiscard]] virtual bool chooser_connected() const = 0;

    /// Starts a job, or returns false if one is already running.
    [[nodiscard]] virtual bool start_job(Job job, JobDone done) = 0;
    [[nodiscard]] virtual bool job_running() const = 0;
    /// Kills the running job. Its callback is never called.
    virtual void cancel_job() = 0;

    /// Sends a request to pixterm-helper, or returns false if one is
    /// already open. done runs with the reply, or with a failed reply
    /// that explains why there is none.
    [[nodiscard]] virtual bool call_helper(helper::Request request, HelperDone done) = 0;
    [[nodiscard]] virtual bool helper_busy() const = 0;

    [[nodiscard]] virtual bool is_socket(const std::filesystem::path& path) const = 0;
    /// Why the file cannot be opened for reading and writing, or no error.
    [[nodiscard]] virtual std::error_code can_open(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual std::optional<std::string>
    read_file(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual bool write_file(const std::filesystem::path& path,
                                          std::string_view data) = 0;
    virtual void remove_file(const std::filesystem::path& path) = 0;

    /// Clears the text console and shows text on it.
    virtual void show_console(std::string_view text) = 0;
    /// Turns the echo of what is typed on the console on or off.
    virtual void console_echo(bool on) = 0;

    [[nodiscard]] virtual std::optional<bool> ethernet_carrier() const = 0;
    /// Clients of the X display: TCP ones only, or all.
    [[nodiscard]] virtual std::optional<int> display_clients(bool tcp_only) const = 0;
};

} // namespace pixterm::supervisor
