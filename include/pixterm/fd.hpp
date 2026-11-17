// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// File descriptors that close themselves, and whole reads and writes
/// that retry after signals and short transfers.

#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace pixterm {

/// Owns one file descriptor and closes it when destroyed.
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

    /// Gives up ownership without closing.
    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    /// Closes the descriptor held, if any, and takes fd instead.
    void reset(int fd = -1) noexcept;

private:
    int fd_ = -1;
};

/// The error of the last failed system call, from errno.
[[nodiscard]] std::error_code last_error() noexcept;

/// Writes all of data, retrying after signals and short writes.
[[nodiscard]] std::expected<void, std::error_code> write_all(int fd,
                                                             std::string_view data) noexcept;

/// Reads until end of file, but no more than limit bytes.
///
/// @return The bytes read, or an error; reading more than limit bytes
///         is the error std::errc::file_too_large.
[[nodiscard]] std::expected<std::string, std::error_code> read_all(int fd, std::size_t limit);

} // namespace pixterm
