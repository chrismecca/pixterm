// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "linux_privileged.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/spawn.hpp>

#include <fcntl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <format>
#include <print>
#include <thread>

namespace pixterm::helper {

namespace {

// The settings partition and USB drives: no device files, no
// set-user-ID programs, nothing run from them.
constexpr unsigned long safe_mount_flags = MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME;
constexpr std::array usb_file_systems{"vfat", "exfat"};
constexpr int request_timeout_seconds = 10;

bool read_only(const std::filesystem::path& dir) {
    struct statvfs v{};
    return ::statvfs(dir.c_str(), &v) == 0 && (v.f_flag & ST_RDONLY) != 0;
}

std::string read_line(const std::filesystem::path& path) {
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd)
        return {};
    return read_all(fd.get(), 64).value_or("");
}

// Partitions of USB mass storage drives, or the whole drive if it has
// none, in the order the kernel lists them.
std::vector<std::string> usb_candidates() {
    std::vector<std::string> found;
    std::error_code ec;
    for (const auto& disk : std::filesystem::directory_iterator("/sys/block", ec)) {
        const auto name = disk.path().filename().string();
        if (!name.starts_with("sd"))
            continue;
        const auto real = std::filesystem::canonical(disk.path(), ec);
        if (ec || !real.string().contains("/usb"))
            continue;
        std::size_t parts = 0;
        for (const auto& entry : std::filesystem::directory_iterator(disk.path(), ec)) {
            const auto part = entry.path().filename().string();
            if (part.starts_with(name) && part != name) {
                found.push_back("/dev/" + part);
                ++parts;
            }
        }
        if (parts == 0)
            found.push_back("/dev/" + name);
    }
    return found;
}

} // namespace

LinuxPrivileged::LinuxPrivileged(Paths paths, uid_t service_uid)
    : paths_(std::move(paths)), service_uid_(service_uid) {}

// The system mounts the settings partition read-only; the helper makes
// it writable only around its own writes.
std::expected<void, std::string> LinuxPrivileged::begin_writing() {
    remounted_ = false;
    if (!read_only(paths_.state_dir))
        return {};
    if (::mount(nullptr, paths_.state_dir.c_str(), nullptr, MS_REMOUNT | safe_mount_flags, nullptr)
        != 0)
        return std::unexpected(std::format("{} cannot be made writable: {}",
                                           paths_.state_dir.string(), last_error().message()));
    remounted_ = true;
    return {};
}

void LinuxPrivileged::finish_writing() {
    if (!remounted_)
        return;
    remounted_ = false;
    ::sync();
    // A file still open for writing makes the remount fail for a moment.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (::mount(nullptr, paths_.state_dir.c_str(), nullptr,
                    MS_REMOUNT | MS_RDONLY | safe_mount_flags, nullptr)
            == 0)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::println(stderr, "pixterm-helper: {} cannot be made read-only again: {}",
                 paths_.state_dir.string(), last_error().message());
}

int LinuxPrivileged::run(const std::vector<std::string>& argv) {
    return run_and_wait(argv, {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C"});
}

std::expected<std::string, std::string> LinuxPrivileged::mount_usb() {
    ::umount2(paths_.usb_dir.c_str(), MNT_DETACH);
    if (::mkdir(paths_.usb_dir.c_str(), 0700) != 0 && errno != EEXIST)
        return std::unexpected(
            std::format("{}: {}", paths_.usb_dir.string(), last_error().message()));
    const auto candidates = usb_candidates();
    if (candidates.empty())
        return std::unexpected(
            "No USB drive was found. Put in a USB drive formatted as FAT and try again.");
    for (const auto& device : candidates)
        for (const auto* type : usb_file_systems)
            if (::mount(device.c_str(), paths_.usb_dir.c_str(), type, safe_mount_flags,
                        "fmask=0177,dmask=0077")
                == 0)
                return device.substr(5); // without "/dev/"
    return std::unexpected(
        "The USB drive could not be read. It must be formatted as FAT or exFAT.");
}

void LinuxPrivileged::unmount_usb() {
    ::sync();
    if (::umount2(paths_.usb_dir.c_str(), 0) != 0)
        ::umount2(paths_.usb_dir.c_str(), MNT_DETACH);
    ::rmdir(paths_.usb_dir.c_str());
}

std::vector<std::string> LinuxPrivileged::ethernet_interfaces() {
    std::vector<std::string> links;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/net", ec)) {
        const auto name = entry.path().filename().string();
        if (name == "lo" || std::filesystem::exists(entry.path() / "wireless", ec))
            continue;
        if (read_line(entry.path() / "type") == "1\n")
            links.push_back(name);
    }
    return links;
}

std::optional<std::string> LinuxPrivileged::wifi_interface() {
    std::vector<std::string> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/net", ec))
        if (std::filesystem::exists(entry.path() / "wireless", ec))
            found.push_back(entry.path().filename().string());
    if (found.empty())
        return std::nullopt;
    // The Pi's own is wlan0; a USB adapter comes after it.
    std::ranges::sort(found);
    return found.front();
}

std::optional<std::string> LinuxPrivileged::run_output(const std::vector<std::string>& argv) {
    constexpr std::size_t limit = std::size_t{256} * 1024;
    return run_for_output(argv, {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", "LANG=C"}, limit);
}

void LinuxPrivileged::pause(std::chrono::milliseconds time) {
    std::this_thread::sleep_for(time);
}

bool LinuxPrivileged::hand_to_service(const std::filesystem::path& path) {
    return service_uid_ == static_cast<uid_t>(-1)
           || ::chown(path.c_str(), service_uid_, static_cast<gid_t>(-1)) == 0;
}

void serve(int fd, Helper& helper, uid_t service_uid) {
    const auto reply = [fd](const Reply& r) {
        (void)write_all(fd, r.ok ? format_ok(r.payload) : format_error(r.message));
    };
    timeval timeout{.tv_sec = request_timeout_seconds, .tv_usec = 0};
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    ucred peer{};
    socklen_t length = sizeof peer;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &length) != 0
        || (peer.uid != 0 && peer.uid != service_uid)) {
        reply({.ok = false, .message = "permission denied", .payload = {}});
        return;
    }

    std::string bytes;
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto request = parse_request(bytes);
        if (request) {
            const auto r = helper.handle(*request);
            if (!r.ok)
                std::println(stderr, "pixterm-helper: {}: {}", request->args.front(), r.message);
            reply(r);
            return;
        }
        if (!request.error().need_more) {
            reply({.ok = false, .message = "a malformed request", .payload = {}});
            return;
        }
        const auto n = ::read(fd, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            reply({.ok = false, .message = "an incomplete request", .payload = {}});
            return;
        }
        bytes.append(buffer.data(), static_cast<std::size_t>(n));
    }
}

std::expected<void, std::string> remove_from_read_only(const std::filesystem::path& path) {
    const auto dir = path.parent_path();
    const bool was_read_only = read_only(dir);
    if (was_read_only
        && ::mount(nullptr, dir.c_str(), nullptr, MS_REMOUNT | safe_mount_flags, nullptr) != 0)
        return std::unexpected(std::format("{}: {}", dir.string(), last_error().message()));
    std::expected<void, std::string> result;
    if (::unlink(path.c_str()) != 0)
        result = std::unexpected(std::format("{}: {}", path.string(), last_error().message()));
    ::sync();
    if (was_read_only)
        ::mount(nullptr, dir.c_str(), nullptr, MS_REMOUNT | MS_RDONLY | safe_mount_flags, nullptr);
    return result;
}

} // namespace pixterm::helper
