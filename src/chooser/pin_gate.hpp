// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The administrator PIN that guards Configure.
///
/// Once the right PIN is entered, the chooser stays unlocked until it
/// exits, which it does at every session start. Three wrong PINs in a
/// row lock the prompt for half a minute.

#pragma once

#include <chrono>
#include <expected>
#include <string>
#include <string_view>

namespace pixterm::chooser {

using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

class PinGate {
public:
    static constexpr int tries = 3;
    static constexpr std::chrono::seconds lockout{30};

    enum class Result {
        unlocked,
        wrong,
        locked_out, ///< too many wrong PINs; the PIN was not checked
    };

    /// Whether Configure must ask for the PIN, given the stored hash
    /// (empty for no PIN).
    [[nodiscard]] bool needs_pin(std::string_view stored) const noexcept {
        return !unlocked_ && !stored.empty();
    }

    /// Checks a PIN typed at time now against the stored hash.
    [[nodiscard]] Result enter(std::string_view pin, std::string_view stored, Time now);

    /// Unlocks without a PIN, after the user has just set one.
    void unlock() noexcept { unlocked_ = true; }

private:
    bool unlocked_ = false;
    int failures_ = 0;
    Time locked_until_{};
};

/// Checks a new PIN, typed twice, and hashes it.
///
/// @return The hash to store, or a sentence for the user.
[[nodiscard]] std::expected<std::string, std::string> new_pin_hash(std::string_view pin,
                                                                   std::string_view again);

} // namespace pixterm::chooser
