// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// rexec and rsh hosts: one command, started on a host too old for SSH,
// opens its windows on this terminal through direct X. The session
// lasts as long as the command. "%D" in the command becomes the display
// name, so "xterm -display %D" works on any host.
//
// The probe checks that the host accepts connections and, for rexec,
// asks for the password, which reaches pixterm-rexec through a pipe and
// is then forgotten. rexec sends the password in clear text; rsh sends
// none, and the host trusts the terminal's address.

#include "parts.hpp"
#include "sessions.hpp"

#include <pixterm/pin.hpp>

#include <format>

namespace pixterm::supervisor {

namespace {

class RemoteExecSession final : public Session {
public:
    using Session::Session;
    ~RemoteExecSession() override { forget_password(); }
    RemoteExecSession(const RemoteExecSession&) = delete;
    RemoteExecSession& operator=(const RemoteExecSession&) = delete;
    RemoteExecSession(RemoteExecSession&&) = delete;
    RemoteExecSession& operator=(RemoteExecSession&&) = delete;

    bool begin(std::string_view username) override;
    void cancel() override {
        sup().log(std::format("connection to {} cancelled", sup().host().name));
        sup().system().cancel_job();
        back_to_chooser();
    }
    void start() override { sup().switch_display(); }
    [[nodiscard]] std::vector<std::string> xorg_args() const override {
        return DirectX::xorg_args();
    }
    void display_ready() override {
        direct_x_.allow(sup(), [this] { run_command(); });
    }
    void child_exited(Slot slot, ExitStatus status, std::string_view output) override;
    void stop() override {
        sup().system().close_input(Slot::client);
        forget_password();
    }
    void cleanup() override { forget_password(); }

private:
    std::string user_;
    std::string password_;
    DirectX direct_x_;

    [[nodiscard]] bool is_rexec() const { return sup().host().protocol == Protocol::rexec; }
    [[nodiscard]] std::string_view kind() const { return is_rexec() ? "rexec" : "rsh"; }
    void forget_password() {
        wipe(password_);
        password_.clear();
    }
    void back_to_chooser() {
        forget_password();
        sup().set_state(State::local);
        sup().chooser_idle();
    }
    void run_command();
};

bool RemoteExecSession::begin(std::string_view username) {
    auto name = login_name(sup().host(), username);
    if (!name) {
        sup().report_error("That user name is not valid.");
        return false;
    }
    user_ = std::move(*name);
    forget_password();
    const bool started = probe_host(sup(), [this](bool reachable) {
        if (!reachable)
            return;
        if (!is_rexec()) {
            sup().start_handoff();
            return;
        }
        const bool asking = ask_password(sup(), user_, [this](std::optional<std::string> password) {
            if (!password) {
                back_to_chooser();
                return;
            }
            password_ = std::move(*password);
            sup().start_handoff();
        });
        if (!asking) {
            sup().report_error("Something else is still in progress; try again.");
            back_to_chooser();
        }
    });
    if (!started) {
        sup().report_error("Something else is still in progress; try again.");
        return false;
    }
    sup().set_state(State::probe);
    return true;
}

void RemoteExecSession::run_command() {
    const auto& h = sup().host();
    std::string command;
    for (std::size_t i = 0; i < h.command.size(); ++i) {
        if (h.command.compare(i, 2, "%D") == 0) {
            command += direct_x_.display();
            ++i;
        } else {
            command += h.command[i];
        }
    }
    Program p{.name = std::string(kind()),
              .argv = {sup().libexec("pixterm-rexec"), std::string(kind()), h.address,
                       std::to_string(h.port), user_, command},
              .env = sup().child_env(),
              .input = is_rexec() ? password_ + '\n' : std::string(),
              .hold_input = true};
    forget_password();
    if (!sup().system().start(Slot::client, std::move(p))) {
        sup().report_error(std::format("Internal error: cannot start {}.", kind()));
        sup().teardown();
        return;
    }
    sup().log(std::format("running \"{}\" on {}", command, h.name));
    sup().session_running(State::session);
}

// pixterm-rexec exits with 2 if the host refused, 3 if it could not
// connect.
void RemoteExecSession::child_exited(Slot slot, ExitStatus status, std::string_view output) {
    if (slot != Slot::client || sup().state() != State::session)
        return;
    const auto& name = sup().host().name;
    if (status.signal == 0 && status.code == 2)
        sup().report_error(std::format("{} refused the command.{}", name, paragraph(output)));
    else if (status.signal == 0 && status.code == 3)
        sup().report_error(std::format("The connection to {} failed.{}", name, paragraph(output)));
    else if (sup().system().now() - sup().started(Slot::client) < session_start_grace)
        sup().report_error(std::format(
            "The command on {} ended immediately.\n\n{}", name,
            output.empty() ? std::string_view("The command must keep running for as long as the "
                                              "session lasts.")
                           : output));
    else
        sup().log(std::format("command on {} ended", name));
    sup().teardown();
}

} // namespace

std::unique_ptr<Session> make_remote_exec_session(Supervisor& sup) {
    return std::make_unique<RemoteExecSession>(sup);
}

} // namespace pixterm::supervisor
