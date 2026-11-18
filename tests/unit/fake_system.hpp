// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// A System for tests: it records what the supervisor asks for, starts
/// nothing, and lets the test say when programs exit, jobs finish and
/// time passes.

#pragma once

#include <supervisor.hpp>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pixterm::test {

using namespace pixterm::supervisor;

class FakeSystem final : public System {
public:
    Time clock{};
    std::vector<std::string> logs;
    Settings settings;
    std::vector<std::vector<std::string>> xorg_starts;
    std::array<std::optional<Program>, slot_count> programs{};
    std::vector<std::pair<Slot, Program>> started_programs;
    std::vector<std::pair<Slot, int>> signals;
    std::vector<std::string> sent;
    bool chooser_open = false;
    std::optional<Job> job;
    JobDone job_done;
    std::optional<helper::Request> helper_request;
    HelperDone helper_done;
    std::map<std::filesystem::path, std::string> files;
    std::set<std::filesystem::path> sockets;
    std::map<std::filesystem::path, std::error_code> devices;
    std::optional<bool> carrier = true;
    std::optional<int> clients = 0;
    std::string console; ///< what tty1 shows
    bool console_echoes = true;
    std::optional<int> tcp_clients = 0;

    // ---- System ----

    [[nodiscard]] Time now() const override { return clock; }
    void log(std::string_view message) override { logs.emplace_back(message); }
    [[nodiscard]] LoadedSettings load_settings() override {
        return {.settings = settings, .problem = {}};
    }

    bool start_xorg(const Settings& /*settings*/, const std::vector<std::string>& args) override {
        xorg_starts.push_back(args);
        programs.at(index(Slot::xorg)) =
            Program{.name = "Xorg", .argv = args, .env = {}, .input = {}, .hold_input = false};
        return true;
    }

    bool start(Slot slot, Program program) override {
        started_programs.emplace_back(slot, program);
        programs.at(index(slot)) = std::move(program);
        return true;
    }

    bool start_chooser(Program program) override {
        chooser_open = true;
        return start(Slot::chooser, std::move(program));
    }

    [[nodiscard]] bool running(Slot slot) const override {
        return programs.at(index(slot)).has_value();
    }
    void signal(Slot slot, int signal) override {
        if (running(slot))
            signals.emplace_back(slot, signal);
    }
    void close_input(Slot /*slot*/) override {}
    void kill_leftovers() override {}

    void send_to_chooser(std::string_view line) override {
        if (chooser_open)
            sent.emplace_back(line);
    }
    void close_chooser() override { chooser_open = false; }
    [[nodiscard]] bool chooser_connected() const override { return chooser_open; }

    bool start_job(Job j, JobDone done) override {
        if (job)
            return false;
        job = std::move(j);
        job_done = std::move(done);
        return true;
    }
    [[nodiscard]] bool job_running() const override { return job.has_value(); }
    void cancel_job() override {
        job.reset();
        job_done = nullptr;
    }

    bool call_helper(helper::Request request, HelperDone done) override {
        if (helper_request)
            return false;
        helper_request = std::move(request);
        helper_done = std::move(done);
        return true;
    }
    [[nodiscard]] bool helper_busy() const override { return helper_request.has_value(); }

    [[nodiscard]] bool is_socket(const std::filesystem::path& path) const override {
        return sockets.contains(path);
    }
    [[nodiscard]] std::error_code can_open(const std::filesystem::path& path) const override {
        const auto it = devices.find(path);
        return it == devices.end() ? std::make_error_code(std::errc::no_such_file_or_directory)
                                   : it->second;
    }
    [[nodiscard]] std::optional<std::string>
    read_file(const std::filesystem::path& path) const override {
        const auto it = files.find(path);
        if (it == files.end())
            return std::nullopt;
        return it->second;
    }
    bool write_file(const std::filesystem::path& path, std::string_view data) override {
        files[path] = data;
        return true;
    }
    void remove_file(const std::filesystem::path& path) override { files.erase(path); }

    void show_console(std::string_view text) override { console = text; }
    void console_echo(bool on) override { console_echoes = on; }

    [[nodiscard]] std::optional<bool> ethernet_carrier() const override { return carrier; }
    [[nodiscard]] std::optional<int> display_clients(bool tcp_only) const override {
        return tcp_only ? tcp_clients : clients;
    }

    // ---- what tests do ----

    static std::size_t index(Slot slot) { return static_cast<std::size_t>(slot); }

    void advance(Supervisor& sup, Duration d) {
        clock += d;
        sup.tick();
    }

    /// The program in a slot exits.
    void exit(Supervisor& sup, Slot slot, ExitStatus status = ExitStatus::exited(0),
              std::string_view output = {}) {
        programs.at(index(slot)).reset();
        if (slot == Slot::chooser)
            chooser_open = false;
        sup.child_exited(slot, status, output);
    }

    /// Every program that was sent a signal exits, Xorg last.
    void reap_signalled(Supervisor& sup) {
        for (const auto slot : {Slot::client, Slot::master, Slot::chooser, Slot::mwm, Slot::xorg}) {
            const bool signalled =
                std::ranges::any_of(signals, [slot](const auto& s) { return s.first == slot; });
            if (signalled && running(slot)) {
                std::erase_if(signals, [slot](const auto& s) { return s.first == slot; });
                exit(sup, slot, ExitStatus::killed(15));
            }
        }
    }

    /// The running job finishes.
    void finish_job(Supervisor& sup, ExitStatus status, std::string_view output) {
        auto done = std::exchange(job_done, nullptr);
        job.reset();
        if (done)
            done(status, output);
        sup.tick();
    }

    void answer_helper(Supervisor& sup, helper::Reply reply) {
        auto done = std::exchange(helper_done, nullptr);
        helper_request.reset();
        if (done)
            done(reply);
        sup.tick();
    }

    [[nodiscard]] const Program* program(Slot slot) const {
        const auto& p = programs.at(index(slot));
        return p ? &*p : nullptr;
    }

    [[nodiscard]] bool was_sent(std::string_view prefix) const {
        return std::ranges::any_of(sent, [prefix](const auto& s) { return s.starts_with(prefix); });
    }

    [[nodiscard]] bool was_signalled(Slot slot) const {
        return std::ranges::any_of(signals, [slot](const auto& s) { return s.first == slot; });
    }

    [[nodiscard]] const Program* job_program() const {
        return job ? std::get_if<Program>(&job->task) : nullptr;
    }
};

/// Whether args holds the words in order, one after another.
inline bool has_sequence(const std::vector<std::string>& args,
                         std::initializer_list<std::string_view> words) {
    const std::vector<std::string_view> want(words);
    return std::ranges::search(args, want,
                               [](const std::string& a, std::string_view b) { return a == b; })
               .size()
           == want.size();
}

/// Whether any of args contains text.
inline bool mentions(const std::vector<std::string>& args, std::string_view text) {
    return std::ranges::any_of(args, [text](const std::string& a) { return a.contains(text); });
}

} // namespace pixterm::test
