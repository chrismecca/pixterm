// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// IBM hosts. Mainframes: 3270 screens over TN3270E, in x3270. The window, with
// no menu bar or keypad, sits in the middle of a white screen in the
// largest 3270 font that fits. x3270 ends when the host disconnects
// (-once); Alt+Q leaves at any time. Port 992 means TLS. The key map,
// "pixterm" in the X resources, adds PC keys to x3270's own.
//
// IBM i: 5250 screens over TN5250E, in tn5250 in a terminal. Its key
// map is the terminal's resources under the name "tn5250". tn5250 ends
// when the host disconnects; port 992 means TLS.

#include "parts.hpp"
#include "sessions.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace pixterm::supervisor {

namespace {

// Model 2: 80 columns by 24 rows, then x3270's status line and margins.
constexpr int columns = 80;
constexpr int rows = 25;
constexpr int margin_width = 16;
constexpr int margin_height = 24;
constexpr int tls_port = 992;

// The 3270gt fonts, largest first: their name's number is the cell's
// height in pixels.
struct Cell {
    int height;
    int width;
};
constexpr std::array cells{Cell{.height = 32, .width = 18}, Cell{.height = 24, .width = 14},
                           Cell{.height = 16, .width = 9}, Cell{.height = 12, .width = 7},
                           Cell{.height = 8, .width = 5}};

int window_width(const Cell& c) {
    return (columns * c.width) + margin_width;
}

int window_height(const Cell& c) {
    return (rows * c.height) + margin_height;
}

const Cell& cell_for(int width, int height) {
    for (const auto& c : cells)
        if (window_width(c) <= width && window_height(c) <= height)
            return c;
    return cells.back();
}

class Tn3270Session final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view /*username*/) override {
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
            const auto& cell = cell_for(w, h);
            const auto& host = sup().host();
            run_desktop(
                sup(),
                {.name = "x3270",
                 .argv = {std::string(programs::x3270), "-once", "-secure", "-model", "3279-2",
                          "-efont", x3270_font(w, h), "-keymap", "pixterm", "-geometry",
                          std::format("+{}+{}", std::max(0, (w - window_width(cell)) / 2),
                                      std::max(0, (h - window_height(cell)) / 2)),
                          std::format("{}{}:{}", host.port == tls_port ? "L:" : "", host.address,
                                      host.port)},
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
};

class Tn5250Session final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view /*username*/) override {
        // IBM i's sign-on screen asks for the user name.
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
        const auto& h = sup().host();
        terminal_.set_resource_name("tn5250");
        // A 5250 screen: 24 rows of 80 columns, and tn5250's status line.
        terminal_.set_size(80, 25);
        terminal_.open(
            sup(),
            {std::string(programs::tn5250),
             std::format("{}{}:{}", h.port == tls_port ? "ssl:" : "", h.address, h.port)},
            sup().child_env(), "");
    }

    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        if (slot == Slot::client)
            terminal_exited(sup(), status, output);
    }

private:
    TerminalWindow terminal_;
};

} // namespace

std::string x3270_font(int width, int height) {
    return std::format("3270gt{}", cell_for(width, height).height);
}

std::unique_ptr<Session> make_tn3270_session(Supervisor& sup) {
    return std::make_unique<Tn3270Session>(sup);
}

std::unique_ptr<Session> make_tn5250_session(Supervisor& sup) {
    return std::make_unique<Tn5250Session>(sup);
}

} // namespace pixterm::supervisor
