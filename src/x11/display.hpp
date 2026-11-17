// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The X display: a connection that closes itself, and the monitor's
/// modes through XRandR. Shared by pixterm-xsetup and the chooser.

#pragma once

#include <pixterm/display_modes.hpp>

#include <X11/Xlib.h>

#include <cstddef>
#include <memory>
#include <optional>

namespace pixterm::x11 {

struct CloseDisplay {
    void operator()(Display* d) const noexcept { XCloseDisplay(d); }
};

/// A connection to the X server, closed when destroyed.
using DisplayPtr = std::unique_ptr<Display, CloseDisplay>;

/// Connects to $DISPLAY, or returns null.
[[nodiscard]] DisplayPtr open_display();

/// The first connected output and the modes its monitor offers.
[[nodiscard]] std::optional<Monitor> query_monitor(Display* display);

/// Switches the monitor to modes[index], resizing the screen.
[[nodiscard]] bool set_mode(Display* display, Monitor& monitor, std::size_t index);

} // namespace pixterm::x11
