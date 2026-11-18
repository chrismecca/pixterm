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
///     wifi on COUNTRY SSIDHEX 0|1   payload: the passphrase, or nothing
///                        to keep the one stored for that network
///     wifi off           forgets the passphrase
///     wifi-scan COUNTRY  reply: the networks found, as scan_reply
///     rdp-cert-save HOST_PORT.pem   payload: an RDP host's certificate
///
/// Every argument and payload is untrusted and checked here. No shell
/// is ever involved; the only programs run are fixed paths with fixed
/// arguments. The privileged operations themselves are behind
/// Privileged, so the requests can be tested without root.

#pragma once

#include <pixterm/helper_protocol.hpp>
#include <pixterm/settings.hpp>

#include <chrono>
#include <expected>
#include <filesystem>
#include <optional>
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
    /// The built-in Wi-Fi interface, if there is one.
    [[nodiscard]] virtual std::optional<std::string> wifi_interface() = 0;
    /// Runs a program with a clean environment and returns what it wrote,
    /// or nothing if it failed.
    [[nodiscard]] virtual std::optional<std::string>
    run_output(const std::vector<std::string>& argv) = 0;
    /// Waits.
    virtual void pause(std::chrono::milliseconds time) = 0;
};

struct Paths {
    std::filesystem::path state_dir; ///< the settings partition
    std::filesystem::path factory_settings;
    std::filesystem::path network_dir; ///< systemd-networkd's, in /run
    std::filesystem::path usb_dir;     ///< where mount_usb mounts the drive
    std::filesystem::path keygen_dir;  ///< private scratch space for ssh-keygen
    std::filesystem::path wifi_dir;    ///< wpa_supplicant's configuration, in /run
};

/// The systemd-networkd file for a network setting.
[[nodiscard]] std::string network_file(const Network& network);

/// Reads "dhcp" or "static ADDRESS GATEWAY|- DNS1|- DNS2|-".
[[nodiscard]] std::expected<Network, std::string>
parse_network_args(std::span<const std::string> args);

/// FreeRDP's name for a host's certificate: "HOST_PORT.pem", the host
/// being a valid address.
[[nodiscard]] bool is_valid_rdp_cert_name(std::string_view name) noexcept;

/// One PEM certificate, and nothing else, of at most 16 KiB.
[[nodiscard]] bool is_valid_pem_certificate(std::string_view text) noexcept;

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
    [[nodiscard]] std::expected<std::string, std::string> wifi(std::span<const std::string> args,
                                                               std::string_view passphrase);
    [[nodiscard]] std::expected<std::string, std::string> wifi_off() const;
    [[nodiscard]] std::expected<std::string, std::string>
    wifi_scan(std::span<const std::string> args);
    [[nodiscard]] std::expected<std::string, std::string>
    save_rdp_cert(std::span<const std::string> args, std::string_view pem);
    [[nodiscard]] std::expected<void, std::string>
    write_wifi_conf(const Wifi& wifi, std::string_view passphrase) const;
    /// The passphrase stored for ssid, if any.
    [[nodiscard]] std::optional<std::string> stored_passphrase(std::string_view ssid) const;
    /// Forgets the passphrase and stops wpa_supplicant.
    [[nodiscard]] std::expected<void, std::string> forget_wifi() const;

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
