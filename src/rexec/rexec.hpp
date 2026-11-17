// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The rexec and rsh protocols, which start one command on a host.
///
/// Both send NUL-terminated strings and get one byte back: 0, or 1
/// followed by the host's reason for refusing.
///
///     rexec (port 512): "0" (no error channel), user, password, command
///     rsh   (port 514): "0", local user, remote user, command, from a
///                       port below 1024, which the host takes as proof
///                       of a trusted system

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace pixterm::rexec {

enum class Kind { rexec, rsh };

[[nodiscard]] std::optional<Kind> parse_kind(std::string_view name) noexcept;

/// The bytes that ask the host to run command. rsh sends no password.
[[nodiscard]] std::string request(Kind kind, std::string_view user, std::string_view password,
                                  std::string_view command);

/// Reads the host's answer: nothing if it accepted, or its reason, up
/// to the end of the line.
[[nodiscard]] std::optional<std::string> refusal(std::string_view answer);

} // namespace pixterm::rexec
