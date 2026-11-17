// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "pin_gate.hpp"

#include <pixterm/pin.hpp>

namespace pixterm::chooser {

PinGate::Result PinGate::enter(std::string_view pin, std::string_view stored, Time now) {
    if (now < locked_until_)
        return Result::locked_out;
    if (!verify_pin(pin, stored)) {
        if (++failures_ >= tries) {
            failures_ = 0;
            locked_until_ = now + lockout;
        }
        return Result::wrong;
    }
    failures_ = 0;
    unlocked_ = true;
    return Result::unlocked;
}

std::expected<std::string, std::string> new_pin_hash(std::string_view pin, std::string_view again) {
    if (!is_valid_pin(pin))
        return std::unexpected("The PIN must be 4 to 12 digits.");
    if (pin != again)
        return std::unexpected("The two PINs do not match.");
    auto hash = make_pin_hash(pin);
    if (!hash)
        return std::unexpected("The PIN could not be stored.");
    return hash;
}

} // namespace pixterm::chooser
