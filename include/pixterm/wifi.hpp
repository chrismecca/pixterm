// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Wi-Fi: what a scan found, how pixterm-helper and the chooser pass it
/// between them, and the wpa_supplicant configuration that joins a
/// network. pixterm joins WPA2- and WPA3-Personal networks.

#pragma once

#include <pixterm/settings.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm {

/// A network a scan found.
struct WifiNetwork {
    std::string ssid;
    int signal = 0;    ///< dBm, such as -50; nearer 0 is stronger
    bool wpa2 = false; ///< offers WPA2-Personal (a pre-shared key)
    bool wpa3 = false; ///< offers WPA3-Personal (SAE)
};

/// The most networks a scan reports.
inline constexpr std::size_t max_wifi_networks = 64;

/// Bytes as lower-case hex, and back. from_hex takes either case.
[[nodiscard]] std::string to_hex(std::string_view bytes);
[[nodiscard]] std::optional<std::string> from_hex(std::string_view hex);

/// The networks in "wpa_cli scan_results" output that pixterm can join,
/// one for each name, strongest first. Open, enterprise and hidden
/// networks are left out.
[[nodiscard]] std::vector<WifiNetwork> parse_scan_results(std::string_view text);

/// Networks as pixterm-helper answers a scan: "SIGNAL SECURITY SSIDHEX"
/// lines, SECURITY being wpa2, wpa3 or wpa2+wpa3.
[[nodiscard]] std::string scan_reply(std::span<const WifiNetwork> networks);
/// Reads scan_reply's text, skipping lines that are not valid.
[[nodiscard]] std::vector<WifiNetwork> parse_scan_reply(std::string_view text);

/// wpa_supplicant's configuration: the country, and the network to join
/// if Wi-Fi is on and passphrase is given. Without one, it only scans.
/// The SSID is written in hex, so any name is safe; the passphrase was
/// checked (printable ASCII, no newline).
[[nodiscard]] std::string wpa_supplicant_conf(const Wifi& wifi, std::string_view passphrase);

} // namespace pixterm
