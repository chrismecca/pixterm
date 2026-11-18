// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Plan 9 and 9front CPU servers, through 9front's drawterm, which
// speaks dp9ik as well as the older p9sk1. drawterm fills the screen and
// asks for the password in its own window, so pixterm never holds it.
// The CPU server is its own authentication server, as on most 9front
// systems. The session ends when the remote rc does.

#include "parts.hpp"
#include "sessions.hpp"

#include <format>

namespace pixterm::supervisor {

namespace {

constexpr int rcpu_port = 17019;

class DrawtermSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view username) override {
        auto name = login_name(sup().host(), username);
        if (!name) {
            sup().report_error("That user name is not valid.");
            return false;
        }
        user_ = std::move(*name);
        if (!probe_host(sup(), [this](bool reachable) {
                if (reachable)
                    sup().start_handoff();
            })) {
            sup().report_error("Something else is still in progress; try again.");
            return false;
        }
        sup().set_state(State::probe);
        return true;
    }

    void cancel() override {
        sup().log(std::format("connection to {} cancelled", sup().host().name));
        sup().system().cancel_job();
        sup().set_state(State::local);
        sup().chooser_idle();
    }

    void start() override {
        const auto sized = [this](ExitStatus status, std::string_view output) {
            if (sup().state() != State::handoff)
                return;
            const auto [w, h] = screen_size(status, output);
            const auto& host = sup().host();
            const auto dial = host.port == rcpu_port
                                  ? host.address
                                  : std::format("tcp!{}!{}", host.address, host.port);
            std::vector<std::string> argv{
                std::string(programs::drawterm), "-h", dial, "-a", host.address, "-g",
                std::format("{}x{}+0+0", w, h)};
            if (!user_.empty())
                argv.insert(argv.end(), {"-u", user_});
            run_desktop(sup(), {.name = "drawterm",
                                .argv = std::move(argv),
                                .env = sup().child_env(),
                                .input = {},
                                .hold_input = false});
        };
        if (!sup().run_xsetup({"--full-screen"}, sized))
            sized(ExitStatus::exited(1), "");
    }

    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        if (slot == Slot::client)
            desktop_exited(sup(), status, output,
                           [](int code, std::string_view) { return code == 0; });
    }

private:
    std::string user_;
};

} // namespace

std::unique_ptr<Session> make_drawterm_session(Supervisor& sup) {
    return std::make_unique<DrawtermSession>(sup);
}

} // namespace pixterm::supervisor
