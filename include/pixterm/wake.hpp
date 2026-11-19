// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Wake-on-LAN: a host asleep on the local network wakes when its
/// network card sees the "magic packet" that holds its MAC address.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pixterm {

using MacAddress = std::array<std::uint8_t, 6>;

/// "00:1a:2b:3c:4d:5e" or "00-1A-2B-3C-4D-5E": six pairs of hex digits,
/// with one kind of separator.
[[nodiscard]] std::optional<MacAddress> parse_mac(std::string_view text);

/// The address as pixterm stores it: lower case, with colons.
[[nodiscard]] std::optional<std::string> normalize_mac(std::string_view text);

/// Six bytes of 0xff, then the address sixteen times: what a network
/// card set for Wake-on-LAN listens for.
[[nodiscard]] std::array<std::uint8_t, 102> magic_packet(const MacAddress& mac);

} // namespace pixterm
