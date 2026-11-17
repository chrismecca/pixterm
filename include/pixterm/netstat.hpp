// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Network state the supervisor watches during sessions, read from
/// Linux's /sys and /proc. Each function returns nothing when it cannot
/// tell, and callers then do not act on it.

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace pixterm {

/// Established connections to a local TCP port, from the text of
/// /proc/net/tcp or /proc/net/tcp6.
[[nodiscard]] int count_established(std::string_view proc_net_tcp, int port);

/// Connected UNIX sockets of X display number display, from the text
/// of /proc/net/unix.
[[nodiscard]] int count_connected_x_sockets(std::string_view proc_net_unix, int display);

/// Whether any wired, physical Ethernet interface has a carrier.
/// Wireless, virtual and loopback interfaces are ignored.
[[nodiscard]] std::optional<bool>
ethernet_carrier(const std::filesystem::path& sys_class_net = "/sys/class/net");

/// Clients of X display number display: TCP connections to its port
/// and connected local sockets.
[[nodiscard]] std::optional<int> display_connections(int display,
                                                     const std::filesystem::path& proc = "/proc");

/// Only the TCP clients of X display number display.
[[nodiscard]] std::optional<int>
display_tcp_connections(int display, const std::filesystem::path& proc = "/proc");

} // namespace pixterm
