// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Facts about the terminal, for the chooser's status line and the
/// System panel.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace pixterm::chooser {

/// A network interface's IPv4 address.
struct Address {
    std::string interface; ///< such as "eth0"
    std::string ipv4;
};

/// The first IPv4 address of an interface that is up and not loopback.
[[nodiscard]] std::optional<Address> primary_address();

/// The status line's network text.
[[nodiscard]] std::string network_status(const std::optional<Address>& address);

/// PRETTY_NAME from the text of /etc/os-release, or "".
[[nodiscard]] std::string os_name(std::string_view os_release);

/// The text of /proc/uptime as "D days, H:MM", or "".
[[nodiscard]] std::string uptime_text(std::string_view proc_uptime);

[[nodiscard]] std::string version_text(std::string_view version, std::string_view build_id);

/// Which settings are in use.
[[nodiscard]] std::string settings_text(std::uint64_t generation);

/// A small file's first line, without its newline or trailing NULs (as
/// in /proc/device-tree/model), or "" if it cannot be read.
[[nodiscard]] std::string first_line(const std::filesystem::path& path);

/// A small file's whole text, or "" if it cannot be read.
[[nodiscard]] std::string small_file(const std::filesystem::path& path);

} // namespace pixterm::chooser
