// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include <pixterm/fd.hpp>

#include <unistd.h>

#include <array>
#include <cerrno>

namespace pixterm {

void UniqueFd::reset(int fd) noexcept {
    // close() is not retried after EINTR: on Linux the descriptor is
    // already gone, and a retry could close one another thread opened.
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = fd;
}

std::error_code last_error() noexcept {
    return {errno, std::system_category()};
}

std::expected<void, std::error_code> write_all(int fd, std::string_view data) noexcept {
    while (!data.empty()) {
        const auto n = ::write(fd, data.data(), data.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return std::unexpected(n < 0 ? last_error()
                                         : std::make_error_code(std::errc::io_error));
        data.remove_prefix(static_cast<std::size_t>(n));
    }
    return {};
}

std::expected<std::string, std::error_code> read_all(int fd, std::size_t limit) {
    std::string out;
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto n = ::read(fd, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            return std::unexpected(last_error());
        if (n == 0)
            return out;
        if (out.size() + static_cast<std::size_t>(n) > limit)
            return std::unexpected(std::make_error_code(std::errc::file_too_large));
        out.append(buffer.data(), static_cast<std::size_t>(n));
    }
}

} // namespace pixterm
