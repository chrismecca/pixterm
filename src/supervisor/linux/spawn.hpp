// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Starting programs.

#pragma once

#include <sys/types.h>

#include <expected>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace pixterm::supervisor {

/// Use pixtermd's own descriptor rather than a new one.
inline constexpr int inherit_fd = -2;

struct SpawnOptions {
    std::vector<std::string> argv; ///< argv[0] is a full path
    std::vector<std::string> env;
    int in = -1;      ///< standard input; -1 is /dev/null
    int out = -1;     ///< standard output; -1 is /dev/null
    int err = -1;     ///< standard error; -1 is /dev/null
    int pass_fd = -1; ///< one more descriptor the program keeps
    /// A session and process group of its own, so the program has no
    /// controlling terminal (ssh then always asks through askpass) and
    /// its whole group can be signalled. Xorg needs pixtermd's terminal.
    bool new_session = true;
};

/// Starts a program. Every descriptor pixtermd opens is close-on-exec,
/// so the program gets only those named here.
[[nodiscard]] std::expected<pid_t, std::error_code> spawn(const SpawnOptions& options);

/// Runs task in a child process in a session of its own, writing what
/// it returns, and a newline, to out.
[[nodiscard]] std::expected<pid_t, std::error_code>
fork_task(const std::function<std::string()>& task, int out);

} // namespace pixterm::supervisor
