// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Changes to the host list. Each makes new settings to hand to
/// pixtermd, and says which row to select once they are saved.

#pragma once

#include <pixterm/settings.hpp>

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace pixterm::chooser {

/// Settings with one change made, and the row to select afterwards.
struct Edit {
    Settings settings;
    std::size_t select = 0;
};

/// Adds a host at the end, with a new id.
///
/// @return The edit, or a sentence for the user if the list is full.
[[nodiscard]] std::expected<Edit, std::string> add_host(const Settings& settings, Host host);

/// Replaces the host at index, keeping its id. index must be in range.
[[nodiscard]] Edit replace_host(const Settings& settings, std::size_t index, Host host);

/// Removes the host at index. index must be in range.
[[nodiscard]] Edit remove_host(const Settings& settings, std::size_t index);

/// Moves the host at index by one row up (-1) or down (+1), or returns
/// nothing if it cannot move that way.
[[nodiscard]] std::optional<Edit> move_host(const Settings& settings, std::size_t index, int by);

/// Which of the list's buttons can be used.
struct ListButtons {
    bool edit = false;
    bool remove = false;
    bool up = false;
    bool down = false;
};

[[nodiscard]] ListButtons list_buttons(std::size_t count, std::optional<std::size_t> selected);

/// Whether an XDMCP host with this address is already in the list.
[[nodiscard]] bool has_xdmcp_host(const Settings& settings, std::string_view address);

} // namespace pixterm::chooser
