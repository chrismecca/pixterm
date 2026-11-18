// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/validate.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <array>
#include <format>

namespace pixterm {
namespace {

using ascii::all_of;
using ascii::parse_decimal;

bool length_within(std::string_view s, std::size_t min, std::size_t max) noexcept {
    return s.size() >= min && s.size() <= max;
}

bool no_control(std::string_view s) noexcept {
    return std::ranges::none_of(s, ascii::is_control);
}

// Letters, digits, '.', '_' and '-', not starting with '-' so that no
// program can read the value as an option.
bool is_name_word(std::string_view s, std::size_t max) noexcept {
    return length_within(s, 1, max) && s.front() != '-' && all_of(s, [](char c) {
               return ascii::is_alnum(c) || c == '.' || c == '_' || c == '-';
           });
}

bool is_xkb_word(std::string_view s, std::size_t min, std::size_t max) noexcept {
    return length_within(s, min, max) && all_of(s, [](char c) {
               return ascii::is_lower(c) || ascii::is_digit(c) || c == '_' || c == '-';
           });
}

// inet_pton needs a terminated string; addresses are short.
bool parses_as(int family, std::string_view s) noexcept {
    std::array<char, INET6_ADDRSTRLEN + 1> text{};
    std::array<unsigned char, sizeof(in6_addr)> out{};
    if (s.size() >= text.size())
        return false;
    s.copy(text.data(), s.size());
    return inet_pton(family, text.data(), out.data()) == 1;
}

} // namespace

bool is_valid_id(std::string_view s) noexcept {
    return length_within(s, 1, limits::id)
           && all_of(s, [](char c) { return ascii::is_alnum(c) || c == '-' || c == '_'; });
}

bool is_valid_name(std::string_view s) noexcept {
    return length_within(s, 1, limits::name) && no_control(s);
}

bool is_valid_username(std::string_view s) noexcept {
    return is_name_word(s, limits::username);
}

bool is_valid_domain(std::string_view s) noexcept {
    return is_name_word(s, limits::domain);
}

bool is_valid_address(std::string_view s) noexcept {
    return length_within(s, 1, limits::address) && s.front() != '-' && s.front() != '.'
           && all_of(s,
                     [](char c) { return ascii::is_alnum(c) || c == '.' || c == '-' || c == ':'; });
}

bool is_valid_serial_device(std::string_view s) noexcept {
    static constexpr std::array<std::string_view, 4> numbered{"/dev/ttyUSB", "/dev/ttyACM",
                                                              "/dev/ttyAMA", "/dev/ttyS"};
    static constexpr std::string_view by_id = "/dev/serial/by-id/";

    if (s.size() > limits::serial_device)
        return false;
    for (const auto prefix : numbered) {
        if (s.starts_with(prefix)) {
            const auto number = s.substr(prefix.size());
            return length_within(number, 1, 3) && all_of(number, ascii::is_digit);
        }
    }
    if (!s.starts_with(by_id))
        return false;
    // A single name with no '/', not starting with '.', so it cannot
    // leave the directory.
    const auto name = s.substr(by_id.size());
    return !name.empty() && name.front() != '.' && all_of(name, [](char c) {
        return ascii::is_alnum(c) || c == '_' || c == '-' || c == '.' || c == ':';
    });
}

bool is_valid_serial_speed(int speed) noexcept {
    static constexpr std::array speeds{300,   1200,  2400,   4800,   9600,   19200,
                                       38400, 57600, 115200, 230400, 460800, 921600};
    return std::ranges::find(speeds, speed) != speeds.end();
}

bool is_valid_display_mode(std::string_view s) noexcept {
    if (s == "auto")
        return true;
    if (s.size() > limits::display_mode)
        return false;
    const auto x = s.find('x');
    const auto at = s.find('@');
    if (x == std::string_view::npos)
        return false;
    const auto w = parse_decimal<unsigned>(s.substr(0, x));
    const auto h = parse_decimal<unsigned>(
        s.substr(x + 1, at == std::string_view::npos ? std::string_view::npos : at - x - 1));
    const auto hz = at == std::string_view::npos ? std::optional<unsigned>(60)
                                                 : parse_decimal<unsigned>(s.substr(at + 1));
    return w && h && hz && *w >= 320 && *w <= 8192 && *h >= 200 && *h <= 8192 && *hz >= 10
           && *hz <= 240;
}

bool is_valid_ssid(std::string_view s) noexcept {
    return length_within(s, 1, limits::ssid) && no_control(s);
}

bool is_valid_wifi_passphrase(std::string_view s) noexcept {
    return length_within(s, 8, limits::wifi_passphrase)
           && all_of(s, [](char c) { return c >= ' ' && c <= '~'; });
}

bool is_valid_country(std::string_view s) noexcept {
    return s.size() == 2 && all_of(s, [](char c) { return c >= 'A' && c <= 'Z'; });
}

bool is_valid_layout(std::string_view s) noexcept {
    return is_xkb_word(s, 2, limits::layout);
}

bool is_valid_variant(std::string_view s) noexcept {
    return is_xkb_word(s, 0, limits::variant);
}

bool is_valid_ip(std::string_view s) noexcept {
    return parses_as(AF_INET, s) || parses_as(AF_INET6, s);
}

bool is_valid_ipv4_cidr(std::string_view s) noexcept {
    const auto slash = s.find('/');
    if (slash == std::string_view::npos)
        return false;
    const auto prefix = parse_decimal<unsigned>(s.substr(slash + 1));
    return parses_as(AF_INET, s.substr(0, slash)) && prefix && *prefix >= 1 && *prefix <= 32;
}

bool is_valid_command(std::string_view s) noexcept {
    return length_within(s, 1, limits::command) && no_control(s);
}

bool is_valid_pin_hash(std::string_view s) noexcept {
    static constexpr std::string_view scheme = "pbkdf2-sha256$";

    if (s.size() > limits::pin_hash || !s.starts_with(scheme))
        return false;
    s.remove_prefix(scheme.size());
    const auto first = s.find('$');
    const auto second = s.find('$', first == std::string_view::npos ? s.size() : first + 1);
    if (second == std::string_view::npos)
        return false;
    const auto iterations = parse_decimal<unsigned long>(s.substr(0, first));
    const auto salt = s.substr(first + 1, second - first - 1);
    const auto hash = s.substr(second + 1);
    return iterations && *iterations >= 1000 && *iterations <= 10'000'000
           && length_within(salt, 16, 128) && salt.size() % 2 == 0
           && all_of(salt, ascii::is_lower_hex) && hash.size() == 64
           && all_of(hash, ascii::is_lower_hex);
}

std::optional<std::string> normalize_font_server(std::string_view s) {
    if (s.starts_with("tcp/"))
        s.remove_prefix(4);
    const auto colon = s.find(':');
    const auto host = s.substr(0, colon);
    unsigned port = 7100;
    if (!is_valid_address(host) || host.contains(':'))
        return std::nullopt;
    if (colon != std::string_view::npos) {
        const auto p = parse_decimal<unsigned>(s.substr(colon + 1));
        if (!p || *p < 1 || *p > 65535)
            return std::nullopt;
        port = *p;
    }
    return std::format("tcp/{}:{}", host, port);
}

bool is_valid_font_server(std::string_view s) {
    return s.size() <= limits::font_server && normalize_font_server(s) == s;
}

} // namespace pixterm
