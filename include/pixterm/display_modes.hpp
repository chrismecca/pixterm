// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Display modes: the sizes and refresh rates a monitor offers, as
/// XRandR reports them, and how the saved setting picks one. Only modes
/// the monitor reports are ever offered or set.

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm {

struct Mode {
    unsigned long id = 0; ///< XRandR's mode id
    unsigned width = 0;
    unsigned height = 0;
    unsigned hz = 0; ///< rounded
    bool preferred = false;

    friend bool operator==(const Mode&, const Mode&) = default;
};

/// The first connected output and its monitor.
struct Monitor {
    std::string output; ///< such as "HDMI-1"
    std::string name;   ///< from the EDID; may be empty
    unsigned long mm_width = 0;
    unsigned long mm_height = 0;
    std::vector<Mode> modes; ///< largest first
    std::optional<std::size_t> current;
};

/// A mode's timings, as XRandR gives them.
struct Timings {
    unsigned long dot_clock = 0;
    unsigned h_total = 0;
    unsigned v_total = 0;
    bool double_scan = false;
    bool interlace = false;
};

/// The refresh rate in whole hertz, or 0 if the timings make no sense.
[[nodiscard]] unsigned refresh_rate(const Timings& t) noexcept;

/// The monitor's name from its EDID's name descriptor, or empty.
[[nodiscard]] std::string edid_monitor_name(std::span<const unsigned char> edid);

/// Sorts modes largest first, then faster first, keeping one of each
/// size and rate: the one in use (current_id) if it is one of them.
[[nodiscard]] std::vector<Mode> sorted_modes(const std::vector<Mode>& modes,
                                             unsigned long current_id);

/// The mode a saved "WxH" or "WxH@Hz" means: that size at that rate,
/// else that size at any rate.
[[nodiscard]] std::optional<std::size_t> find_mode(const Monitor& m, std::string_view saved);

/// The mode "automatic" means: the monitor's preferred one, else the
/// largest nearest the panel's shape.
[[nodiscard]] std::optional<std::size_t> automatic_mode(const Monitor& m);

/// "1920x1080@60", as the settings store it.
[[nodiscard]] std::string mode_name(const Mode& m);
/// "1920 x 1080   60 Hz", as Configure shows it.
[[nodiscard]] std::string mode_label(const Mode& m);

} // namespace pixterm
