// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// ASCII character classes. Unlike <cctype>, these never depend on the
/// locale, so untrusted input is checked the same way everywhere.

#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>

namespace pixterm::ascii {

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] constexpr bool is_lower(char c) noexcept {
    return c >= 'a' && c <= 'z';
}
[[nodiscard]] constexpr bool is_upper(char c) noexcept {
    return c >= 'A' && c <= 'Z';
}
[[nodiscard]] constexpr bool is_alnum(char c) noexcept {
    return is_digit(c) || is_lower(c) || is_upper(c);
}
[[nodiscard]] constexpr bool is_lower_hex(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f');
}
[[nodiscard]] constexpr bool is_control(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u < 0x20 || u == 0x7f;
}

/// True if every character of s satisfies pred.
template <typename Pred>
[[nodiscard]] constexpr bool all_of(std::string_view s, Pred pred) {
    return std::ranges::all_of(s, pred);
}

/// Parses a whole string of decimal digits, with no sign or spaces.
template <typename Int>
[[nodiscard]] constexpr std::optional<Int> parse_decimal(std::string_view s) noexcept {
    if (s.empty() || !all_of(s, is_digit))
        return std::nullopt;
    Int value{};
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || end != s.data() + s.size())
        return std::nullopt;
    return value;
}

} // namespace pixterm::ascii
