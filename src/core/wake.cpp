// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include <pixterm/wake.hpp>

#include <format>

namespace pixterm {

namespace {

std::optional<std::uint8_t> hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return static_cast<std::uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<std::uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F')
        return static_cast<std::uint8_t>(c - 'A' + 10);
    return std::nullopt;
}

} // namespace

std::optional<MacAddress> parse_mac(std::string_view text) {
    constexpr std::size_t length = 17; // six pairs and five separators
    if (text.size() != length || (text[2] != ':' && text[2] != '-'))
        return std::nullopt;
    const char separator = text[2];
    MacAddress mac{};
    for (std::size_t i = 0; i < mac.size(); ++i) {
        const auto at = i * 3;
        if (i > 0 && text[at - 1] != separator)
            return std::nullopt;
        const auto high = hex_digit(text[at]);
        const auto low = hex_digit(text[at + 1]);
        if (!high || !low)
            return std::nullopt;
        mac.at(i) = static_cast<std::uint8_t>((*high << 4) | *low);
    }
    return mac;
}

std::optional<std::string> normalize_mac(std::string_view text) {
    const auto mac = parse_mac(text);
    if (!mac)
        return std::nullopt;
    return std::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}", (*mac)[0], (*mac)[1], (*mac)[2],
                       (*mac)[3], (*mac)[4], (*mac)[5]);
}

std::array<std::uint8_t, 102> magic_packet(const MacAddress& mac) {
    std::array<std::uint8_t, 102> packet{};
    for (std::size_t i = 0; i < 6; ++i)
        packet.at(i) = 0xff;
    for (std::size_t i = 6; i < packet.size(); ++i)
        packet.at(i) = mac.at((i - 6) % mac.size());
    return packet;
}

} // namespace pixterm
