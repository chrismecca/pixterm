// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Checks for every field a host entry or the terminal's settings can
/// hold. Settings may come from a USB drive, so they are untrusted: the
/// supervisor, the helper and the chooser all check them with these
/// functions before use.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace pixterm {

/// The longest value, in bytes, each field may hold.
namespace limits {
inline constexpr std::size_t id = 64;
inline constexpr std::size_t name = 64;
inline constexpr std::size_t address = 253;
inline constexpr std::size_t username = 32;
inline constexpr std::size_t domain = 63;
inline constexpr std::size_t serial_device = 96;
inline constexpr std::size_t layout = 32;
inline constexpr std::size_t variant = 32;
inline constexpr std::size_t display_mode = 24;
inline constexpr std::size_t font_server = 270;
inline constexpr std::size_t command = 512;
inline constexpr std::size_t pin_hash = 200;
inline constexpr std::size_t ssid = 32; ///< bytes, as 802.11 allows
inline constexpr std::size_t wifi_passphrase = 63;
inline constexpr std::size_t time_zone = 64;
} // namespace limits

/// A host identifier: letters, digits, '-' and '_'.
[[nodiscard]] bool is_valid_id(std::string_view s) noexcept;

/// A name shown in the chooser: any text without control characters.
[[nodiscard]] bool is_valid_name(std::string_view s) noexcept;

/// A POSIX portable user name that cannot be read as an option.
[[nodiscard]] bool is_valid_username(std::string_view s) noexcept;

/// A Windows domain for RDP logins.
[[nodiscard]] bool is_valid_domain(std::string_view s) noexcept;

/// A host name or IP literal that cannot be read as an option.
[[nodiscard]] bool is_valid_address(std::string_view s) noexcept;

/// A serial port of the terminal: /dev/ttyUSBn, /dev/ttyACMn,
/// /dev/ttyAMAn, /dev/ttySn, or a name under /dev/serial/by-id/.
[[nodiscard]] bool is_valid_serial_device(std::string_view s) noexcept;

/// A standard serial line speed from 300 to 921600 bits per second.
[[nodiscard]] bool is_valid_serial_speed(int speed) noexcept;

/// "auto", "WxH" or "WxH@Hz", within the sizes a monitor can show.
[[nodiscard]] bool is_valid_display_mode(std::string_view s) noexcept;

/// An XKB layout name such as "us".
[[nodiscard]] bool is_valid_layout(std::string_view s) noexcept;

/// An XKB variant name such as "dvorak", or empty.
[[nodiscard]] bool is_valid_variant(std::string_view s) noexcept;

/// An IPv4 or IPv6 address literal.
[[nodiscard]] bool is_valid_ip(std::string_view s) noexcept;

/// An IPv4 address with a prefix length, such as "192.168.1.20/24".
[[nodiscard]] bool is_valid_ipv4_cidr(std::string_view s) noexcept;

/// One line of printable text. Commands only ever reach a remote host.
[[nodiscard]] bool is_valid_command(std::string_view s) noexcept;

/// A stored PIN hash, as make_pin_hash writes it.
[[nodiscard]] bool is_valid_pin_hash(std::string_view s) noexcept;

/// A Wi-Fi network's name: 1 to 32 bytes (often UTF-8) without control
/// characters.
[[nodiscard]] bool is_valid_ssid(std::string_view s) noexcept;

/// A WPA2 or WPA3-Personal passphrase: 8 to 63 printable ASCII
/// characters.
[[nodiscard]] bool is_valid_wifi_passphrase(std::string_view s) noexcept;

/// An ISO 3166 country code, such as "GB": two capital letters.
[[nodiscard]] bool is_valid_country(std::string_view s) noexcept;

/// An IANA time zone name, such as "Europe/London": letters, digits,
/// '_', '+', '-' and '/' between names, never ".." or a leading '/'.
[[nodiscard]] bool is_valid_time_zone(std::string_view s) noexcept;

/// Converts a font server given as "host", "host:port" or
/// "tcp/host:port" to "tcp/host:port".
///
/// @return The normal form, or nothing if the input is not a font server.
[[nodiscard]] std::optional<std::string> normalize_font_server(std::string_view s);

/// A font server already in the form normalize_font_server returns.
[[nodiscard]] bool is_valid_font_server(std::string_view s);

} // namespace pixterm
