// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// What the Display, Keyboard and Network panels of Configure show, and
/// how what is typed there becomes settings.

#pragma once

#include <pixterm/display_modes.hpp>
#include <pixterm/settings.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace pixterm::chooser {

// ---- Display ----

/// The rows of the mode list: "Automatic", then each mode.
[[nodiscard]] std::vector<std::string> mode_rows(const Monitor& m);

/// The row showing a saved display mode setting; row 0 if the monitor
/// does not offer it.
[[nodiscard]] std::size_t mode_row(const Monitor& m, std::string_view saved);

/// What choosing a row means.
struct ModePick {
    std::string setting;             ///< what to save, such as "auto"
    std::optional<std::size_t> mode; ///< the mode to switch to, if any
};

[[nodiscard]] ModePick pick_mode(const Monitor& m, std::size_t row);

[[nodiscard]] std::string monitor_text(const std::optional<Monitor>& m);
[[nodiscard]] std::string current_mode_text(const Monitor& m);

/// The question asked after trying a new mode.
[[nodiscard]] std::string keep_mode_question(int seconds_left);

/// How long the user has to keep a new mode before it is undone.
inline constexpr int mode_trial_seconds = 15;

// ---- Keyboard ----

struct KeyboardLayout {
    std::string_view code; ///< the XKB layout
    std::string_view name;
};

inline constexpr std::array<KeyboardLayout, 34> keyboard_layouts{{
    {"us", "English (US)"},
    {"gb", "English (UK)"},
    {"ca", "Canadian"},
    {"de", "German"},
    {"ch", "Swiss"},
    {"at", "Austrian"},
    {"fr", "French"},
    {"be", "Belgian"},
    {"nl", "Dutch"},
    {"es", "Spanish"},
    {"latam", "Latin American"},
    {"pt", "Portuguese"},
    {"br", "Portuguese (Brazil)"},
    {"it", "Italian"},
    {"dk", "Danish"},
    {"no", "Norwegian"},
    {"se", "Swedish"},
    {"fi", "Finnish"},
    {"is", "Icelandic"},
    {"ie", "Irish"},
    {"pl", "Polish"},
    {"cz", "Czech"},
    {"sk", "Slovak"},
    {"hu", "Hungarian"},
    {"si", "Slovenian"},
    {"hr", "Croatian"},
    {"ro", "Romanian"},
    {"gr", "Greek"},
    {"tr", "Turkish"},
    {"ru", "Russian"},
    {"ua", "Ukrainian"},
    {"il", "Hebrew"},
    {"jp", "Japanese"},
    {"kr", "Korean"},
}};

/// The list row of a layout code, if it is listed.
[[nodiscard]] std::optional<std::size_t> layout_row(std::string_view code);
[[nodiscard]] std::string layout_line(const KeyboardLayout& layout);

/// Checks a chosen layout and a typed variant.
///
/// @return The layout and the trimmed variant, or a sentence for the
///         user.
[[nodiscard]] std::expected<std::pair<std::string, std::string>, std::string>
read_keyboard(std::string_view layout, std::string_view variant);

// ---- Network ----

/// The Network panel's fields, as typed.
struct NetworkForm {
    bool is_static = false;
    std::string address;
    std::string gateway;
    std::string dns1;
    std::string dns2;
};

[[nodiscard]] NetworkForm network_form(const Network& n);

/// Checks the form.
///
/// @return The network settings, or a sentence for the user.
[[nodiscard]] std::expected<Network, std::string> read_network_form(const NetworkForm& form);

} // namespace pixterm::chooser
