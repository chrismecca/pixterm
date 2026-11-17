// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Serial consoles: a terminal running picocom on one of the terminal's
// own serial ports, such as a USB adapter wired to a server's console.
// The port is checked before the chooser closes. picocom's file
// transfer keys, which would run local commands, are off. Ctrl+A Ctrl+\
// sends a BREAK (Stop-A on a Sun); Ctrl+A Ctrl+X ends the session.

#include "parts.hpp"
#include "sessions.hpp"

#include <format>

namespace pixterm::supervisor {

namespace {

class SerialSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view /*username*/) override {
        const auto& h = sup().host();
        if (const auto ec = sup().system().can_open(h.device)) {
            if (ec == std::errc::no_such_file_or_directory)
                sup().report_error(std::format(
                    "There is no serial port {}.\n\nCheck that the USB serial adapter is "
                    "plugged in. The first adapter is usually /dev/ttyUSB0, or /dev/ttyACM0 for "
                    "some.",
                    h.device));
            else
                sup().report_error(std::format("The serial port {} cannot be opened: {}.", h.device,
                                               ec.message()));
            return false;
        }
        sup().log(std::format("serial console {} on {} at {}", h.name, h.device, h.speed));
        sup().start_handoff();
        return true;
    }

    void start() override {
        const auto& h = sup().host();
        const auto speed = std::to_string(h.speed);
        // 8 data bits, no parity, one stop bit, no flow control.
        terminal_.open(sup(),
                       {std::string(programs::picocom), "--baud", speed, "--databits", "8",
                        "--parity", "n", "--stopbits", "1", "--flow", "n", "--send-cmd", "",
                        "--receive-cmd", "", h.device},
                       sup().child_env(),
                       std::format("{} at {}; Ctrl+A Ctrl+X ends, Ctrl+A Ctrl+\\ sends BREAK",
                                   h.device, h.speed));
    }

    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        if (slot == Slot::client)
            terminal_exited(sup(), status, output);
    }

private:
    TerminalWindow terminal_;
};

} // namespace

std::unique_ptr<Session> make_serial_session(Supervisor& sup) {
    return std::make_unique<SerialSession>(sup);
}

} // namespace pixterm::supervisor
