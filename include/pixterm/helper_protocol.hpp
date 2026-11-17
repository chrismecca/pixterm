// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The messages between the supervisor and pixterm-helper, the one
/// program that runs as root. One request per connection over a UNIX
/// stream socket:
///
///     request:  "<payload length> <command> [<argument>...]\n" <payload>
///     reply:    "OK <payload length>\n" <payload>
///               "ERR <escaped text>\n"
///
/// Words are printable ASCII, separated by single spaces. Payloads are
/// settings or known_hosts text. The helper treats every byte as
/// untrusted, and accepts only requests in exactly the form
/// format_request writes.

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::helper {

inline constexpr std::string_view socket_path = "/run/pixterm-helper.sock";
inline constexpr std::size_t max_line_length = 512;
inline constexpr std::size_t max_arguments = 8;
inline constexpr std::size_t max_payload = 64 * 1024;

struct Request {
    std::vector<std::string> args; ///< the command, then its arguments
    std::string payload;
};

struct Reply {
    bool ok = false;
    std::string message; ///< why the request failed
    std::string payload;
};

/// Why bytes are not (yet) a whole message.
struct ParseError {
    bool need_more = false; ///< the bytes so far are a valid start
    std::string message;    ///< otherwise, what is wrong
};

template <typename T>
using Parsed = std::expected<T, ParseError>;

/// The bytes of a request, or an error if an argument is empty, holds
/// anything but printable ASCII, or the request is too large.
[[nodiscard]] std::expected<std::string, std::string> format_request(const Request& request);

[[nodiscard]] Parsed<Request> parse_request(std::string_view bytes);

[[nodiscard]] std::string format_ok(std::string_view payload);
[[nodiscard]] std::string format_error(std::string_view message);

[[nodiscard]] Parsed<Reply> parse_reply(std::string_view bytes);

} // namespace pixterm::helper
