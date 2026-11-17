// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// pixterm-helper's requests: the only things pixterm does as root.
///
///     reboot
///     network dhcp
///     network static ADDRESS/PREFIX GATEWAY|- DNS1|- DNS2|-
///     config-save        payload: settings JSON
///     config-reset
///     known-hosts-save   payload: known_hosts text
///     usb-export         reply: a message for the user
///     usb-import
///     keygen             reply: the terminal's public key
///
/// Every argument and payload is untrusted and checked here. No shell
/// is ever involved; the only programs run are fixed paths with fixed
/// arguments. The privileged operations themselves are behind
/// Privileged, so the requests can be tested without root.

#pragma once

#include <pixterm/helper_protocol.hpp>
#include <pixterm/settings.hpp>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::helper {

/// What needs root.
class Privileged {
public:
    Privileged() = default;
    virtual ~Privileged() = default;
    Privileged(const Privileged&) = delete;
    Privileged& operator=(const Privileged&) = delete;
    Privileged(Privileged&&) = delete;
    Privileged& operator=(Privileged&&) = delete;

    /// Makes the settings partition writable, until finish_writing.
    [[nodiscard]] virtual std::expected<void, std::string> begin_writing() = 0;
    /// Makes it read-only again, flushing what was written.
    virtual void finish_writing() = 0;
    /// Runs a program with a clean environment; returns its exit status.
    [[nodiscard]] virtual int run(const std::vector<std::string>& argv) = 0;
    /// Mounts the first readable USB drive at Paths::usb_dir, without
    /// device files, set-user-ID or execution.
    ///
    /// @return The drive's name, such as "sda1", or why none could be
    ///         mounted, as a sentence for the user.
    [[nodiscard]] virtual std::expected<std::string, std::string> mount_usb() = 0;
    virtual void unmount_usb() = 0;
    /// The wired Ethernet interfaces.
    [[nodiscard]] virtual std::vector<std::string> ethernet_interfaces() = 0;
    /// Gives a file to the pixterm account, which runs ssh.
    [[nodiscard]] virtual bool hand_to_service(const std::filesystem::path& path) = 0;
};

struct Paths {
    std::filesystem::path state_dir; ///< the settings partition
    std::filesystem::path factory_settings;
    std::filesystem::path network_dir; ///< systemd-networkd's, in /run
    std::filesystem::path usb_dir;     ///< where mount_usb mounts the drive
    std::filesystem::path keygen_dir;  ///< private scratch space for ssh-keygen
};

/// The systemd-networkd file for a network setting.
[[nodiscard]] std::string network_file(const Network& network);

/// Reads "dhcp" or "static ADDRESS GATEWAY|- DNS1|- DNS2|-".
[[nodiscard]] std::expected<Network, std::string>
parse_network_args(std::span<const std::string> args);

/// known_hosts text: lines of printable ASCII, none too long.
[[nodiscard]] bool is_valid_known_hosts(std::string_view text) noexcept;

class Helper {
public:
    Helper(Paths paths, Privileged& privileged);

    /// Carries out one request.
    [[nodiscard]] Reply handle(const Request& request);

    /// At boot: a factory reset if asked for, then the saved network
    /// settings.
    [[nodiscard]] std::expected<void, std::string> boot(bool reset);

private:
    Paths paths_;
    Privileged& privileged_;

    [[nodiscard]] std::expected<std::string, std::string> dispatch(const Request& request);
    [[nodiscard]] std::expected<void, std::string> write_network(const Network& network) const;
    [[nodiscard]] std::expected<std::string, std::string>
    apply_network(std::span<const std::string> args);
    [[nodiscard]] std::expected<std::string, std::string> save_settings(std::string_view json);
    [[nodiscard]] std::expected<void, std::string> store(const Settings& settings) const;
    [[nodiscard]] std::expected<std::string, std::string> save_known_hosts(std::string_view text);
    [[nodiscard]] std::expected<void, std::string> reset() const;
    [[nodiscard]] std::expected<std::string, std::string> keygen();
    [[nodiscard]] std::expected<std::string, std::string> usb_export();
    [[nodiscard]] std::expected<std::string, std::string> usb_import();
    [[nodiscard]] std::expected<std::string, std::string> reboot();

    /// Runs body with the settings partition writable.
    template <typename Body>
    auto writing(Body body) -> decltype(body());
};

/// Replaces dir/name with data: a temporary file, flushed, then
/// renamed over it, and the directory flushed.
[[nodiscard]] std::expected<void, std::string> replace_file(const std::filesystem::path& dir,
                                                            std::string_view name,
                                                            std::string_view data, unsigned mode);

/// Why a file could not be read.
struct ReadError {
    bool missing = false; ///< it does not exist
    std::string message;
};

/// Reads a regular file of at most limit bytes, refusing links.
[[nodiscard]] std::expected<std::string, ReadError>
read_regular_file(const std::filesystem::path& path, std::size_t limit);

} // namespace pixterm::helper
