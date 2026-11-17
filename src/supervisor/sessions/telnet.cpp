// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Telnet hosts: a terminal running telnet. The probe checks that the
// host accepts connections; the host's own login prompt then appears in
// the terminal. Telnet sends everything, passwords too, in clear text.

#include "parts.hpp"
#include "sessions.hpp"

#include <format>

namespace pixterm::supervisor {

namespace {

class TelnetSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view /*username*/) override {
        // The host asks for the user name itself.
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
        if (sup().host().opens_windows_directly())
            sup().switch_display(); // then display_ready
        else
            start_terminal();
    }

    [[nodiscard]] std::vector<std::string> xorg_args() const override {
        return DirectX::xorg_args();
    }

    void display_ready() override {
        direct_x_.allow(sup(), [this] { start_terminal(); });
    }

    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        if (slot == Slot::client)
            terminal_exited(sup(), status, output);
    }

private:
    DirectX direct_x_;
    TerminalWindow terminal_;

    void start_terminal() {
        const auto& h = sup().host();
        // -8: eight-bit characters, which DEC terminals use. -E: no escape
        // character, so no telnet> prompt and no way to a local shell.
        terminal_.open(
            sup(), {std::string(programs::telnet), "-8", "-E", h.address, std::to_string(h.port)},
            sup().child_env(),
            h.opens_windows_directly() && !direct_x_.display().empty()
                ? "X display " + direct_x_.display()
                : std::string());
    }
};

} // namespace

std::unique_ptr<Session> make_telnet_session(Supervisor& sup) {
    return std::make_unique<TelnetSession>(sup);
}

} // namespace pixterm::supervisor
