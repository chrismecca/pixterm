// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The line protocol between the chooser (pixterm-ui) and the
/// supervisor (pixtermd), over a socket pair the chooser inherits.
///
/// Each message is one line of at most max_line_length bytes: a word
/// naming the message, then its arguments. Free text is escaped so that
/// it stays on one line. The supervisor checks every request against
/// the settings it loaded itself.

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pixterm {

inline constexpr std::size_t max_line_length = 1024;

/// The environment variable that names the chooser's end of the socket
/// pair.
inline constexpr std::string_view control_fd_variable = "PIXTERM_CONTROL_FD";

/// Escapes text so it fits in one line: '\\' and newline become "\\\\"
/// and "\\n", carriage returns are dropped and other control characters
/// become spaces.
[[nodiscard]] std::string escape_line(std::string_view text);

/// Reverses escape_line.
[[nodiscard]] std::string unescape_line(std::string_view text);

/// The words of a line, split at spaces.
[[nodiscard]] std::vector<std::string_view> split_words(std::string_view line);

/// Collects bytes from a stream into lines.
class LineSplitter {
public:
    /// Adds bytes and returns the lines they complete, without their
    /// newlines. A line longer than max_line_length is dropped whole.
    [[nodiscard]] std::vector<std::string> feed(std::string_view data);

private:
    std::string partial_;
    bool discarding_ = false;
};

/// Sends one line; the newline is added.
///
/// @return An error if the line holds a newline or is too long, or if
///         writing fails.
[[nodiscard]] std::expected<void, std::error_code> send_line(int fd, std::string_view line);

} // namespace pixterm
