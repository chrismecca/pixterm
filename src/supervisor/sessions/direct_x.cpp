// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Direct X hosts open their windows on the terminal themselves, as
// Genera does. A notice waits for the first window, and the session
// ends a few seconds after the last one closes. Ctrl+Alt+Backspace
// ends it at once.

#include "parts.hpp"
#include "sessions.hpp"

#include <csignal>
#include <format>

namespace pixterm::supervisor {

namespace {

constexpr Duration idle_end{5'000};

class DirectXSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view /*username*/) override {
        sup().start_handoff();
        return true;
    }

    void start() override { sup().switch_display(); }

    [[nodiscard]] std::vector<std::string> xorg_args() const override {
        return DirectX::xorg_args();
    }

    void display_ready() override {
        direct_x_.allow(sup(), [this] { wait_for_windows(); });
    }

    void watch(Time now) override {
        if (sup().state() != State::remote)
            return;
        const auto clients = sup().system().display_clients(0, true);
        if (clients > 0) {
            if (!seen_) {
                sup().log(std::format("{} opened a window", sup().host().name));
                sup().system().signal(Slot::chooser, SIGTERM); // the notice
            }
            seen_ = true;
            last_seen_ = now;
        } else if (clients == 0 && seen_ && now - last_seen_ >= idle_end) {
            sup().log(std::format("direct X session on {} ended", sup().host().name));
            sup().teardown();
        }
    }

    void xorg_exited(ExitStatus /*status*/) override {
        sup().log(std::format("direct X session on {} ended", sup().host().name));
    }

private:
    DirectX direct_x_;
    bool seen_ = false;
    Time last_seen_;

    void wait_for_windows() {
        seen_ = false;
        sup().session_running(State::remote);
        const auto& name = sup().host().name;
        sup().log(std::format("waiting for windows from {}", name));
        sup().start_notice(std::format(
            "Waiting for windows from {}.\n\nOn the host, use the display {}.\n"
            "Ctrl+Alt+Backspace returns to the host list.",
            name, direct_x_.display().empty() ? "of this terminal" : direct_x_.display()));
    }
};

} // namespace

std::unique_ptr<Session> make_direct_x_session(Supervisor& sup) {
    return std::make_unique<DirectXSession>(sup);
}

} // namespace pixterm::supervisor
