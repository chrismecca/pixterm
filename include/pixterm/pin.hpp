// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The administrator PIN that may guard the Configure dialog.
///
/// The settings hold only a salted PBKDF2-SHA256 hash, as
/// "pbkdf2-sha256$ITERATIONS$SALT$HASH" with the salt and hash in hex.
/// A PIN keeps passers-by out of the settings; it does not protect
/// against anyone who can take out the SD card.

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace pixterm {

inline constexpr std::size_t min_pin_length = 4;
inline constexpr std::size_t max_pin_length = 12;
inline constexpr unsigned pin_iterations = 100'000;

/// Four to twelve decimal digits.
[[nodiscard]] bool is_valid_pin(std::string_view pin) noexcept;

/// Hashes a PIN with a fresh random salt.
///
/// @return The text to store, or a message if the PIN is not valid or
///         no random bytes could be had.
[[nodiscard]] std::expected<std::string, std::string> make_pin_hash(std::string_view pin);

/// Whether pin matches a stored hash. Compares in constant time.
[[nodiscard]] bool verify_pin(std::string_view pin, std::string_view stored);

/// Overwrites memory that held a secret, in a way the compiler cannot
/// remove.
void wipe(std::span<char> secret) noexcept;

} // namespace pixterm
