// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The System pixtermd runs on: real processes, pipes and sockets on
/// Linux, driven by an EventLoop.

#pragma once

#include "../system.hpp"
#include "event_loop.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/lines.hpp>

#include <sys/types.h>

#include <array>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>

namespace pixterm::supervisor {

/// Where LinuxSystem finds its programs and keeps its files.
struct LinuxPaths {
    std::filesystem::path xorg;       ///< the X server
    std::filesystem::path xauth;      ///< the xauth program
    std::filesystem::path xauthority; ///< the X server's cookie file
    std::filesystem::path xorg_log;
    std::filesystem::path xorg_conf; ///< read by Xorg at start
    std::filesystem::path log_file;  ///< recent log lines, for Diagnostics
    std::filesystem::path helper_socket;
    std::filesystem::path state_dir; ///< saved settings
    std::filesystem::path factory_settings;
    std::vector<std::string> env; ///< for xauth
};

class LinuxSystem final : public System {
public:
    LinuxSystem(EventLoop& loop, LinuxPaths paths);
    ~LinuxSystem() override;
    LinuxSystem(const LinuxSystem&) = delete;
    LinuxSystem& operator=(const LinuxSystem&) = delete;
    LinuxSystem(LinuxSystem&&) = delete;
    LinuxSystem& operator=(LinuxSystem&&) = delete;

    /// Where to report what happens. Must be set before anything starts.
    void set_events(Events& events) { events_ = &events; }
    /// Uses fd, open for reading and writing (pixtermd's tty), as the text
    /// console. Without one, the console shows and reads nothing.
    void use_console(int fd);

    /// Collects every child that has exited. Call on SIGCHLD.
    void reap();
    /// When tick must next be called for a job or helper timeout.
    [[nodiscard]] std::optional<Time> next_deadline() const;
    void tick();
    /// Ends every program, waiting a few seconds before killing them.
    /// Used when pixtermd itself is told to stop.
    void terminate_all();

    [[nodiscard]] Time now() const override { return Clock::now(); }
    void log(std::string_view message) override;
    [[nodiscard]] LoadedSettings load_settings() override;

    [[nodiscard]] bool start_xorg(const Settings& settings,
                                  const std::vector<std::string>& extra_args) override;
    [[nodiscard]] bool start(Slot slot, Program program) override;
    [[nodiscard]] bool start_chooser(Program program) override;
    [[nodiscard]] bool running(Slot slot) const override;
    void signal(Slot slot, int signal) override;
    void close_input(Slot slot) override;
    void kill_leftovers() override;

    void send_to_chooser(std::string_view line) override;
    void close_chooser() override;
    [[nodiscard]] bool chooser_connected() const override { return static_cast<bool>(chooser_); }

    [[nodiscard]] bool start_job(Job job, JobDone done) override;
    [[nodiscard]] bool job_running() const override { return job_.pid > 0; }
    void cancel_job() override;

    [[nodiscard]] bool call_helper(helper::Request request, HelperDone done) override;
    [[nodiscard]] bool helper_busy() const override { return static_cast<bool>(helper_.socket); }

    [[nodiscard]] bool is_socket(const std::filesystem::path& path) const override;
    [[nodiscard]] std::error_code can_open(const std::filesystem::path& path) const override;
    [[nodiscard]] std::optional<std::string>
    read_file(const std::filesystem::path& path) const override;
    [[nodiscard]] bool write_file(const std::filesystem::path& path,
                                  std::string_view data) override;
    void remove_file(const std::filesystem::path& path) override;

    void show_console(std::string_view text) override;
    void console_echo(bool on) override;
    [[nodiscard]] std::optional<bool> ethernet_carrier() const override;
    [[nodiscard]] std::optional<int> display_clients(bool tcp_only) const override;

private:
    struct Child {
        std::string name;
        pid_t pid = 0;
        pid_t group = 0; ///< kept after the program exits, for kill_leftovers
        UniqueFd output;
        int output_watch = -1;
        std::string tail;
        UniqueFd input;
    };

    struct RunningJob {
        std::string name;
        pid_t pid = 0;
        UniqueFd output;
        int output_watch = -1;
        std::string text;
        Time deadline{};
        bool secret = false;
        JobDone done;
    };

    struct HelperCall {
        UniqueFd socket;
        int watch = -1;
        std::string request;
        std::size_t sent = 0;
        std::string reply;
        Time deadline{};
        HelperDone done;
    };

    EventLoop& loop_;
    LinuxPaths paths_;
    Events* events_ = nullptr;
    std::array<Child, slot_count> children_;
    UniqueFd display_fd_; ///< Xorg writes its display number here when ready
    int display_watch_ = -1;
    std::string display_text_;
    UniqueFd chooser_;
    int chooser_watch_ = -1;
    LineSplitter chooser_lines_;
    int console_ = -1;
    int console_watch_ = -1;
    LineSplitter console_lines_;
    RunningJob job_;
    HelperCall helper_;
    std::deque<std::string> recent_log_;

    Child& child(Slot slot) { return children_.at(static_cast<std::size_t>(slot)); }
    [[nodiscard]] const Child& child(Slot slot) const {
        return children_.at(static_cast<std::size_t>(slot));
    }
    [[nodiscard]] bool write_xauthority();
    void write_xorg_conf(const Settings& settings);
    void capture(Child& c, int read_end);
    void read_output(Child& c);
    void child_reaped(Slot slot, int status);
    void job_reaped(int status);
    void helper_io(short revents);
    void helper_finish(std::optional<std::string> transport_error);
};

} // namespace pixterm::supervisor
