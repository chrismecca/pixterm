// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The root-only operations, on Linux.

#pragma once

#include "helper.hpp"

#include <sys/types.h>

namespace pixterm::helper {

class LinuxPrivileged final : public Privileged {
public:
    /// service_uid is the pixterm account's, or -1 if there is none.
    LinuxPrivileged(Paths paths, uid_t service_uid);

    std::expected<void, std::string> begin_writing() override;
    void finish_writing() override;
    int run(const std::vector<std::string>& argv) override;
    std::expected<std::string, std::string> mount_usb() override;
    void unmount_usb() override;
    std::vector<std::string> ethernet_interfaces() override;
    bool hand_to_service(const std::filesystem::path& path) override;

private:
    Paths paths_;
    uid_t service_uid_;
    bool remounted_ = false;
};

/// Serves one request on a connected socket: checks that the peer is
/// root or the pixterm account, reads the request, and replies.
void serve(int fd, Helper& helper, uid_t service_uid);

/// Makes the directory holding path writable for a moment, removes
/// path, and makes it read-only again. Used for the factory reset
/// marker on the boot partition.
[[nodiscard]] std::expected<void, std::string>
remove_from_read_only(const std::filesystem::path& path);

} // namespace pixterm::helper
