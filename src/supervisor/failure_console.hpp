// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The text console on tty1, used only while the X server cannot start.
/// After failures_before_console failed starts in a row it says why,
/// from Xorg's log, and offers two ways out: Enter tries again at once,
/// and "reset" restores the factory settings and restarts the terminal,
/// after the administrator PIN if one is set. The X server's screen
/// covers the console once it starts.

#pragma once

#include "system.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::supervisor {

class Supervisor;

/// Failed X starts in a row before the console explains them.
inline constexpr int failures_before_console = 3;

/// How long the console refuses a reset after a wrong PIN.
inline constexpr Duration wrong_pin_lockout{30'000};

/// The last max errors in an Xorg log, without the lines every fatal
/// error adds (where the log is, how the server ended).
[[nodiscard]] std::vector<std::string> xorg_errors(std::string_view log, std::size_t max);

class FailureConsole {
public:
    explicit FailureConsole(Supervisor& supervisor) : sup_(supervisor) {}

    /// The X server failed to start, failures times in a row.
    void x_failed(int failures);
    /// The X server is up and covers the console.
    void x_ready();
    /// A line typed on the console.
    void line(std::string_view text);

private:
    enum class Mode { hidden, menu, pin, busy };

    Supervisor& sup_;
    Mode mode_ = Mode::hidden;
    int failures_ = 0;
    std::optional<Time> locked_until_;

    /// Shows why X fails and what can be done, after note if any.
    void show_menu(std::string_view note = {});
    void ask_pin();
    void reset();
};

} // namespace pixterm::supervisor
