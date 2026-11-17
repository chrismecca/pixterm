// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "host_form.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>

namespace pixterm::chooser {

namespace {

constexpr std::array<std::string_view, all_protocols.size()> menu_labels{
    "SSH / X11", "XDMCP", "SSH terminal", "Telnet", "Direct X (the host opens windows)",
    "rexec",     "rsh",   "VNC",          "RDP",    "Serial console",
};

constexpr std::array<std::string_view, all_protocols.size()> list_labels{
    "SSH", "XDMCP", "Terminal", "Telnet", "Direct X", "rexec", "rsh", "VNC", "RDP", "Serial",
};

std::size_t index(Protocol p) noexcept {
    return static_cast<std::size_t>(p);
}

std::string_view trim(std::string_view s) noexcept {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && blank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && blank(s.back()))
        s.remove_suffix(1);
    return s;
}

// A whole decimal number, or nothing.
std::optional<int> number(std::string_view s) noexcept {
    int value = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || ec != std::errc{} || ptr != s.data() + s.size())
        return std::nullopt;
    return value;
}

std::unexpected<std::string> problem(std::string_view text) {
    return std::unexpected(std::string(text));
}

} // namespace

EnabledFields enabled_fields(Protocol p, SessionType s) noexcept {
    const bool serial = p == Protocol::serial;
    const bool ssh_x11 = p == Protocol::ssh_x11;
    return {.address = !serial,
            .port = !serial && p != Protocol::direct_x,
            .device = serial,
            .speed = serial,
            .username = wants_user(p),
            .domain = p == Protocol::rdp,
            .key_auth = is_ssh(p),
            .allow_x = is_terminal(p),
            .session = ssh_x11,
            .command = p == Protocol::rexec || p == Protocol::rsh || (ssh_x11 && takes_command(s)),
            .indirect = p == Protocol::xdmcp,
            .font_server = is_x11(p)};
}

HostForm form_for(const Host* host) {
    const Host fresh;
    const Host& h = host != nullptr ? *host : fresh;
    return {.name = h.name,
            .address = h.address,
            .port = std::to_string(h.protocol == Protocol::serial ? 22 : h.port),
            .username = h.username,
            .domain = h.domain,
            .device = h.device.empty() ? "/dev/ttyUSB0" : h.device,
            .speed = std::to_string(h.speed),
            .command = h.command,
            .font_server = h.font_server,
            .protocol = h.protocol,
            .session = h.session,
            .key_auth = h.auth == Auth::key,
            .allow_x = h.allow_x,
            .indirect = h.xdmcp_mode == XdmcpMode::indirect};
}

std::string port_after_change(std::string_view port, Protocol p) {
    const int usual = default_port(p);
    if (usual == 0)
        return std::string(port);
    const auto current = number(trim(port));
    const bool was_usual = current && std::ranges::any_of(all_protocols, [&](Protocol q) {
                               return default_port(q) == *current;
                           });
    if (trim(port).empty() || was_usual)
        return std::to_string(usual);
    return std::string(port);
}

std::expected<Host, std::string> read_form(const HostForm& form) {
    const auto fields = enabled_fields(form.protocol, form.session);
    Host h;
    h.protocol = form.protocol;
    h.port = default_port(h.protocol);

    h.name = trim(form.name);
    if (h.name.empty() || !is_valid_name(h.name))
        return problem("Please enter a name for this host.");

    if (h.protocol == Protocol::serial) {
        h.device = trim(form.device);
        if (!is_valid_serial_device(h.device))
            return problem("The serial device must look like /dev/ttyUSB0, /dev/ttyACM0\n"
                           "or a name under /dev/serial/by-id/.");
        const auto speed = number(trim(form.speed));
        if (!speed || !is_valid_serial_speed(*speed))
            return problem("The speed must be a standard one, such as 9600 or 115200.");
        h.speed = *speed;
    } else {
        h.address = trim(form.address);
        if (!is_valid_address(h.address))
            return problem("Please enter a valid host name or IP address.");
        if (fields.port) {
            const auto port = number(trim(form.port));
            if (!port || *port < 1 || *port > 65535)
                return problem("The port must be a number from 1 to 65535.");
            h.port = *port;
        }
    }

    if (fields.username) {
        h.username = trim(form.username);
        if (!h.username.empty() && !is_valid_username(h.username))
            return problem("The user name may contain letters, digits, '.', '_' and '-'.");
    }
    if (fields.domain) {
        h.domain = trim(form.domain);
        if (!h.domain.empty() && !is_valid_domain(h.domain))
            return problem("The domain may contain letters, digits, '.', '_' and '-'.");
    }
    if (fields.key_auth && form.key_auth)
        h.auth = Auth::key;
    if (fields.allow_x)
        h.allow_x = form.allow_x;
    if (fields.session)
        h.session = form.session;
    if (fields.command) {
        h.command = trim(form.command);
        if (h.command.empty() || !is_valid_command(h.command))
            return problem(h.protocol == Protocol::ssh_x11
                               ? "Please enter the command that starts the session on the host."
                               : "Please enter the command to run on the host, such as\n"
                                 "xterm -display %D");
    }
    if (fields.indirect && form.indirect)
        h.xdmcp_mode = XdmcpMode::indirect;
    if (fields.font_server && !trim(form.font_server).empty()) {
        auto normal = normalize_font_server(trim(form.font_server));
        if (!normal)
            return problem("The font server must look like host or host:7100.");
        h.font_server = std::move(*normal);
    }
    return h;
}

std::string_view protocol_menu_label(Protocol p) noexcept {
    return menu_labels.at(index(p));
}

std::string_view protocol_list_label(Protocol p) noexcept {
    return list_labels.at(index(p));
}

std::string host_line(const Host& h) {
    constexpr std::string_view dev = "/dev/";
    std::string_view where = h.address;
    if (h.protocol == Protocol::serial) {
        where = h.device;
        if (where.starts_with(dev))
            where.remove_prefix(dev.size());
    }
    const auto column = [](std::string_view s, std::size_t width) {
        return std::format("{:<{}}", s.substr(0, width), width);
    };
    return std::format("{} {} {}", column(h.name, 16), column(where, 15),
                       protocol_list_label(h.protocol));
}

bool needs_username(const Host& h) noexcept {
    return wants_user(h.protocol) && h.username.empty();
}

} // namespace pixterm::chooser
