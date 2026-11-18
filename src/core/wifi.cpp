// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/validate.hpp>
#include <pixterm/wifi.hpp>

#include <algorithm>
#include <format>
#include <ranges>

namespace pixterm {

namespace {

std::optional<int> hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return std::nullopt;
}

// Reverses wpa_supplicant's printf_encode, which escapes SSIDs in its
// output: \\, \", \e, \n, \r, \t and \xHH.
std::optional<std::string> wpa_unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') {
            out += s[i];
            continue;
        }
        if (++i >= s.size())
            return std::nullopt;
        switch (s[i]) {
        case '\\':
        case '"':
            out += s[i];
            break;
        case 'e':
            out += '\x1b';
            break;
        case 'n':
            out += '\n';
            break;
        case 'r':
            out += '\r';
            break;
        case 't':
            out += '\t';
            break;
        case 'x': {
            if (i + 2 >= s.size())
                return std::nullopt;
            const auto byte = from_hex(s.substr(i + 1, 2));
            if (!byte || byte->size() != 1)
                return std::nullopt;
            out += *byte;
            i += 2;
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return out;
}

// A signal level such as "-48".
std::optional<int> parse_signal(std::string_view s) {
    const bool negative = s.starts_with('-');
    const auto magnitude = ascii::parse_decimal<int>(negative ? s.substr(1) : s);
    if (!magnitude)
        return std::nullopt;
    return negative ? -*magnitude : *magnitude;
}

std::vector<std::string_view> split(std::string_view s, char by) {
    std::vector<std::string_view> parts;
    for (const auto part : std::views::split(s, by))
        parts.emplace_back(part.begin(), part.end());
    return parts;
}

// One entry for each name, with the strongest signal and everything
// offered under it; strongest first.
std::vector<WifiNetwork> merged(std::vector<WifiNetwork> found) {
    std::vector<WifiNetwork> out;
    for (auto& n : found) {
        auto it = std::ranges::find(out, n.ssid, &WifiNetwork::ssid);
        if (it == out.end()) {
            out.push_back(std::move(n));
            continue;
        }
        it->signal = std::max(it->signal, n.signal);
        it->wpa2 = it->wpa2 || n.wpa2;
        it->wpa3 = it->wpa3 || n.wpa3;
    }
    std::ranges::stable_sort(out, std::ranges::greater{}, &WifiNetwork::signal);
    if (out.size() > max_wifi_networks)
        out.resize(max_wifi_networks);
    return out;
}

std::string_view security_word(const WifiNetwork& n) {
    if (n.wpa2 && n.wpa3)
        return "wpa2+wpa3";
    return n.wpa3 ? "wpa3" : "wpa2";
}

} // namespace

std::string to_hex(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes)
        out += std::format("{:02x}", static_cast<unsigned char>(c));
    return out;
}

std::optional<std::string> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0)
        return std::nullopt;
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto high = hex_digit(hex[i]);
        const auto low = hex_digit(hex[i + 1]);
        if (!high || !low)
            return std::nullopt;
        out += static_cast<char>((*high << 4) | *low);
    }
    return out;
}

std::vector<WifiNetwork> parse_scan_results(std::string_view text) {
    std::vector<WifiNetwork> found;
    for (const auto line : split(text, '\n')) {
        const auto fields = split(line, '\t');
        if (fields.size() != 5)
            continue;
        const auto signal = parse_signal(fields[2]);
        auto ssid = wpa_unescape(fields[4]);
        if (!signal || !ssid || !is_valid_ssid(*ssid))
            continue;
        const auto flags = fields[3];
        WifiNetwork n{.ssid = std::move(*ssid),
                      .signal = *signal,
                      .wpa2 = flags.contains("PSK"),
                      .wpa3 = flags.contains("SAE")};
        if (n.wpa2 || n.wpa3)
            found.push_back(std::move(n));
    }
    return merged(std::move(found));
}

std::string scan_reply(std::span<const WifiNetwork> networks) {
    std::string out;
    for (const auto& n : networks)
        out += std::format("{} {} {}\n", n.signal, security_word(n), to_hex(n.ssid));
    return out;
}

std::vector<WifiNetwork> parse_scan_reply(std::string_view text) {
    std::vector<WifiNetwork> found;
    for (const auto line : split(text, '\n')) {
        const auto fields = split(line, ' ');
        if (fields.size() != 3)
            continue;
        const auto signal = parse_signal(fields[0]);
        auto ssid = from_hex(fields[2]);
        const auto security = fields[1];
        if (!signal || !ssid || !is_valid_ssid(*ssid)
            || (security != "wpa2" && security != "wpa3" && security != "wpa2+wpa3"))
            continue;
        found.push_back({.ssid = std::move(*ssid),
                         .signal = *signal,
                         .wpa2 = security.contains("wpa2"),
                         .wpa3 = security.contains("wpa3")});
    }
    return merged(std::move(found));
}

std::string wpa_supplicant_conf(const Wifi& wifi, std::string_view passphrase) {
    auto text = std::format("# Written by pixterm-helper from the terminal's settings.\n"
                            "ctrl_interface=DIR=/run/wpa_supplicant GROUP=root\n"
                            "update_config=0\n"
                            "country={}\n",
                            wifi.country);
    if (!wifi.enabled || passphrase.empty())
        return text;
    // WPA2 (PSK) and WPA3 (SAE) alike: wpa_supplicant uses what the
    // network offers, and protects management frames where it can.
    text += std::format("\nnetwork={{\n"
                        "\tssid={}\n"
                        "{}"
                        "\tkey_mgmt=WPA-PSK WPA-PSK-SHA256 SAE\n"
                        "\tieee80211w=1\n"
                        "\tpsk=\"{}\"\n"
                        "\tsae_password=\"{}\"\n"
                        "}}\n",
                        to_hex(wifi.ssid), wifi.hidden ? "\tscan_ssid=1\n" : "", passphrase,
                        passphrase);
    return text;
}

} // namespace pixterm
