// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The small part of XDMCP 1.0 that pixterm speaks itself: the queries
/// that find display managers, and their Willing and Unwilling answers.
/// Xorg runs the rest of the protocol.
///
/// Answers are UDP datagrams anyone on the network can send, so the
/// parser is strict and bounds and cleans every string.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pixterm::xdmcp {

inline constexpr std::uint16_t port = 177;

/// The longest host name or status text kept from an answer.
inline constexpr std::size_t max_text = 120;

enum class QueryKind {
    broadcast, ///< BroadcastQuery, to every manager on the network
    direct,    ///< Query, to one manager
    indirect,  ///< IndirectQuery, for the manager's chooser
};

/// The datagram for a query with no authentication names.
[[nodiscard]] std::vector<unsigned char> encode_query(QueryKind kind);

/// A display manager's answer to a query.
struct Reply {
    bool willing = false;
    std::string hostname; ///< as the manager names itself
    std::string status;   ///< such as the load average, or why not

    friend bool operator==(const Reply&, const Reply&) = default;
};

/// Reads a Willing or Unwilling datagram. Text is cut to max_text
/// characters, characters outside printable ASCII become '?', and
/// trailing spaces are removed.
///
/// @return The reply, or nothing for any other or malformed datagram.
[[nodiscard]] std::optional<Reply> parse_reply(std::span<const unsigned char> datagram);

} // namespace pixterm::xdmcp
