// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include <pixterm/json.hpp>
#include <pixterm/settings.hpp>

#include <sys/random.h>

#include <algorithm>
#include <format>
#include <ranges>

namespace pixterm {

namespace {

struct ProtocolInfo {
    Protocol protocol;
    std::string_view name;
    int port;
};

constexpr std::array<ProtocolInfo, all_protocols.size()> protocols{{
    {.protocol = Protocol::ssh_x11, .name = "ssh-x11", .port = 22},
    {.protocol = Protocol::xdmcp, .name = "xdmcp", .port = 177},
    {.protocol = Protocol::ssh_terminal, .name = "ssh-terminal", .port = 22},
    {.protocol = Protocol::telnet, .name = "telnet", .port = 23},
    {.protocol = Protocol::direct_x, .name = "direct-x", .port = 6000},
    {.protocol = Protocol::rexec, .name = "rexec", .port = 512},
    {.protocol = Protocol::rsh, .name = "rsh", .port = 514},
    {.protocol = Protocol::vnc, .name = "vnc", .port = 5900},
    {.protocol = Protocol::rdp, .name = "rdp", .port = 3389},
    {.protocol = Protocol::serial, .name = "serial", .port = 0},
    {.protocol = Protocol::tn3270, .name = "tn3270", .port = 23},
}};

const ProtocolInfo& info(Protocol p) noexcept {
    return protocols.at(static_cast<std::size_t>(p));
}

struct SessionInfo {
    SessionType type;
    std::string_view name;
    std::string_view label;
};

constexpr std::array sessions{
    SessionInfo{
        .type = SessionType::xsession, .name = "xsession", .label = "Your X session (~/.xsession)"},
    SessionInfo{.type = SessionType::xterm, .name = "xterm", .label = "Single xterm (diagnostic)"},
    SessionInfo{.type = SessionType::custom, .name = "custom", .label = "Custom command"},
    SessionInfo{.type = SessionType::raw, .name = "raw", .label = "Raw command (VMS, non-Unix)"},
};

const SessionInfo& info(SessionType t) noexcept {
    return sessions.at(static_cast<std::size_t>(t));
}

using Error = std::unexpected<std::string>;

template <typename... Args>
Error error(std::format_string<Args...> fmt, Args&&... args) {
    return std::unexpected(std::format(fmt, std::forward<Args>(args)...));
}

// A string member. A missing member reads as empty, the same as an
// empty string; a member of another type is an error.
std::expected<std::string_view, std::string> string_member(const json::Value* object,
                                                           std::string_view key) {
    const auto* v = object != nullptr ? object->find(key) : nullptr;
    if (v == nullptr)
        return std::string_view{};
    if (const auto s = v->as_string())
        return *s;
    return error("{} must be text", key);
}

// A whole-number member, or fallback if it is missing.
std::expected<std::int64_t, std::string>
integer_member(const json::Value& object, std::string_view key, std::int64_t fallback) {
    const auto* v = object.find(key);
    if (v == nullptr)
        return fallback;
    if (const auto i = v->as_integer())
        return *i;
    return error("{} must be a whole number", key);
}

// A string member that must pass valid; an empty one is allowed only
// if optional is set.
std::expected<std::string, std::string> checked_member(const json::Value* object,
                                                       std::string_view key,
                                                       bool (*valid)(std::string_view) noexcept,
                                                       bool optional) {
    const auto s = string_member(object, key);
    if (!s || (s->empty() && !optional) || (!s->empty() && !valid(*s)))
        return error("invalid or missing {}", key);
    return std::string(*s);
}

// A string member that must be one of the given words, or missing.
std::expected<std::string_view, std::string>
word_member(const json::Value* object, std::string_view key,
            std::initializer_list<std::string_view> words) {
    const auto s = string_member(object, key);
    if (!s || (!s->empty() && std::ranges::find(words, *s) == words.end()))
        return error("invalid {}", key);
    return *s;
}

// Reads one host. Each option is checked only for the protocols it
// applies to; options for other protocols are ignored.
std::expected<Host, std::string> parse_host(const json::Value& v, std::size_t index) {
    if (v.as_object() == nullptr)
        return error("host {}: not an object", index + 1);
    Host h;

    auto id = checked_member(&v, "id", is_valid_id, false);
    if (!id)
        return error("host {}: invalid or missing id", index + 1);
    h.id = std::move(*id);
    const auto fail = [&h](std::string_view what) { return error("host {}: {}", h.id, what); };

    auto name = checked_member(&v, "name", is_valid_name, false);
    if (!name)
        return fail(name.error());
    h.name = std::move(*name);

    const auto protocol = string_member(&v, "protocol");
    const auto p = protocol ? protocol_from_name(*protocol) : std::nullopt;
    if (!p)
        return fail("unsupported or missing protocol");
    h.protocol = *p;
    h.port = default_port(h.protocol);

    if (h.protocol == Protocol::serial) {
        auto device = checked_member(&v, "device", is_valid_serial_device, false);
        if (!device)
            return fail("invalid or missing serial device");
        h.device = std::move(*device);
        const auto speed = integer_member(v, "speed", h.speed);
        if (!speed || *speed < 0 || *speed > 1'000'000
            || !is_valid_serial_speed(static_cast<int>(*speed)))
            return fail("invalid serial speed");
        h.speed = static_cast<int>(*speed);
    } else {
        auto address = checked_member(&v, "address", is_valid_address, false);
        if (!address)
            return fail(address.error());
        h.address = std::move(*address);
        const auto port = integer_member(v, "port", h.port);
        if (!port || *port < 1 || *port > 65535)
            return fail("invalid port");
        h.port = static_cast<int>(*port);
    }

    if (wants_user(h.protocol)) {
        auto user = checked_member(&v, "username", is_valid_username, true);
        if (!user)
            return fail("invalid username");
        h.username = std::move(*user);
    }

    if (is_ssh(h.protocol)) {
        const auto auth = word_member(&v, "auth", {"password", "key"});
        if (!auth)
            return fail(auth.error());
        h.auth = *auth == "key" ? Auth::key : Auth::password;
    }

    if (h.protocol == Protocol::ssh_x11) {
        const auto session = string_member(&v, "session");
        const auto type =
            session ? session_from_name(session->empty() ? "xsession" : *session) : std::nullopt;
        if (!type)
            return fail("unknown session type");
        h.session = *type;
    }

    if (h.protocol == Protocol::rexec || h.protocol == Protocol::rsh
        || (h.protocol == Protocol::ssh_x11 && takes_command(h.session))) {
        auto command = checked_member(&v, "command", is_valid_command, false);
        if (!command)
            return fail(command.error());
        h.command = std::move(*command);
    }

    if (h.protocol == Protocol::xdmcp) {
        const auto mode = word_member(&v, "xdmcp_mode", {"query", "indirect"});
        if (!mode)
            return fail(mode.error());
        h.xdmcp_mode = *mode == "indirect" ? XdmcpMode::indirect : XdmcpMode::query;
    }

    if (is_x11(h.protocol)) {
        const auto fs = string_member(&v, "font_server");
        if (!fs)
            return fail(fs.error());
        if (!fs->empty()) {
            auto normal = normalize_font_server(*fs);
            if (!normal)
                return fail("invalid font_server");
            h.font_server = std::move(*normal);
        }
    }

    if (is_terminal(h.protocol)) {
        if (const auto* ax = v.find("allow_x")) {
            const auto b = ax->as_bool();
            if (!b)
                return fail("invalid allow_x");
            h.allow_x = *b;
        }
    }

    if (h.protocol == Protocol::rdp) {
        auto domain = checked_member(&v, "domain", is_valid_domain, true);
        if (!domain)
            return fail(domain.error());
        h.domain = std::move(*domain);
    }
    return h;
}

std::expected<Network, std::string> parse_network(const json::Value* v) {
    Network n;
    const auto method = word_member(v, "method", {"dhcp", "static"});
    if (!method)
        return error("network: {}", method.error());
    if (*method != "static")
        return n;
    n.is_static = true;

    const auto address = string_member(v, "address");
    const auto gateway = string_member(v, "gateway");
    if (!address || !gateway)
        return error("network: invalid address or gateway");
    n.address = *address;
    n.gateway = *gateway;
    if (const auto* dns = v->find("dns")) {
        const auto* list = dns->as_array();
        if (list == nullptr || list->size() > max_dns_servers)
            return error("network: invalid dns list");
        for (const auto& server : *list) {
            const auto s = server.as_string();
            if (!s)
                return error("network: invalid dns list");
            n.dns.emplace_back(*s);
        }
    }
    if (auto problem = network_problem(n))
        return error("network: {}", *problem);
    return n;
}

std::expected<Wifi, std::string> parse_wifi(const json::Value& v) {
    if (v.find("passphrase") != nullptr)
        return error("wifi: the passphrase is not kept in the settings");
    Wifi w;
    if (const auto* enabled = v.find("enabled")) {
        const auto b = enabled->as_bool();
        if (!b)
            return error("wifi: invalid enabled");
        w.enabled = *b;
    }
    if (const auto* hidden = v.find("hidden")) {
        const auto b = hidden->as_bool();
        if (!b)
            return error("wifi: invalid hidden");
        w.hidden = *b;
    }
    auto ssid = checked_member(&v, "ssid", is_valid_ssid, true);
    auto country = checked_member(&v, "country", is_valid_country, true);
    if (!ssid || !country)
        return error("wifi: {}", !ssid ? ssid.error() : country.error());
    w.ssid = std::move(*ssid);
    w.country = std::move(*country);
    if (w.enabled && (w.ssid.empty() || w.country.empty()))
        return error("wifi: a network and a country are needed");
    return w;
}

std::expected<Settings, std::string> parse_document(const json::Value& root) {
    Settings s;
    if (root.as_object() == nullptr)
        return error("the settings are not a JSON object");
    const auto* version = root.find("schema_version");
    if (version == nullptr || version->as_integer() != schema_version)
        return error("unsupported schema_version");

    const auto* device = root.find("device");
    const auto* keyboard = device != nullptr ? device->find("keyboard") : nullptr;
    const auto* display = device != nullptr ? device->find("display") : nullptr;

    auto layout = checked_member(keyboard, "layout", is_valid_layout, true);
    auto variant = checked_member(keyboard, "variant", is_valid_variant, true);
    auto mode = checked_member(display, "mode", is_valid_display_mode, true);
    auto pin = checked_member(device, "admin_pin", is_valid_pin_hash, true);
    if (!layout)
        return error("invalid keyboard layout");
    if (!variant)
        return error("invalid keyboard variant");
    if (!mode)
        return error("invalid display mode");
    if (!pin)
        return error("invalid admin_pin");
    if (!layout->empty())
        s.keyboard_layout = std::move(*layout);
    s.keyboard_variant = std::move(*variant);
    if (!mode->empty())
        s.display_mode = std::move(*mode);
    if (display != nullptr) {
        const auto blank = integer_member(*display, "blank_minutes", s.blank_minutes);
        if (!blank || *blank < 0 || *blank > max_blank_minutes)
            return error("invalid blank_minutes");
        s.blank_minutes = static_cast<int>(*blank);
    }
    s.admin_pin = std::move(*pin);

    if (const auto* wifi = device != nullptr ? device->find("wifi") : nullptr) {
        auto w = parse_wifi(*wifi);
        if (!w)
            return std::unexpected(w.error());
        s.wifi = std::move(*w);
    }

    if (const auto* network = device != nullptr ? device->find("network") : nullptr) {
        auto n = parse_network(network);
        if (!n)
            return std::unexpected(n.error());
        s.network = std::move(*n);
    }

    if (const auto* hosts = root.find("hosts")) {
        const auto* list = hosts->as_array();
        if (list == nullptr)
            return error("hosts is not a list");
        if (list->size() > max_hosts)
            return error("too many hosts (at most {})", max_hosts);
        for (const auto& [i, v] : std::views::zip(std::views::iota(std::size_t{0}), *list)) {
            auto h = parse_host(v, i);
            if (!h)
                return std::unexpected(h.error());
            if (s.find_host(h->id) != nullptr)
                return error("host id {} appears twice", h->id);
            s.hosts.push_back(std::move(*h));
        }
    }
    return s;
}

json::Value host_to_json(const Host& h) {
    json::Object o;
    const auto add = [&o](std::string key, json::Value v) {
        o.push_back({.key = std::move(key), .value = std::move(v)});
    };
    add("id", h.id);
    add("name", h.name);
    add("protocol", protocol_name(h.protocol));
    if (h.protocol == Protocol::serial) {
        add("device", h.device);
        add("speed", h.speed);
    } else {
        add("address", h.address);
        add("port", h.port);
    }
    if (wants_user(h.protocol))
        add("username", h.username);
    if (is_ssh(h.protocol))
        add("auth", h.auth == Auth::key ? "key" : "password");
    if (h.protocol == Protocol::ssh_x11)
        add("session", session_name(h.session));
    if ((h.protocol == Protocol::ssh_x11 && takes_command(h.session))
        || h.protocol == Protocol::rexec || h.protocol == Protocol::rsh)
        add("command", h.command);
    if (h.protocol == Protocol::xdmcp)
        add("xdmcp_mode", h.xdmcp_mode == XdmcpMode::indirect ? "indirect" : "query");
    if (is_x11(h.protocol) && !h.font_server.empty())
        add("font_server", h.font_server);
    if (is_terminal(h.protocol) && h.allow_x)
        add("allow_x", true);
    if (h.protocol == Protocol::rdp && !h.domain.empty())
        add("domain", h.domain);
    return o;
}

// Checks that X11 forwarding is on before running anything.
constexpr std::string_view require_display =
    R"([ -n "$DISPLAY" ] || { echo "pixterm: X11 forwarding is not enabled on this host" >&2; exit 126; }; )";

} // namespace

std::string_view protocol_name(Protocol p) noexcept {
    return info(p).name;
}

std::optional<Protocol> protocol_from_name(std::string_view name) noexcept {
    const auto* const it = std::ranges::find(protocols, name, &ProtocolInfo::name);
    if (it == protocols.end())
        return std::nullopt;
    return it->protocol;
}

int default_port(Protocol p) noexcept {
    return info(p).port;
}

bool is_ssh(Protocol p) noexcept {
    return p == Protocol::ssh_x11 || p == Protocol::ssh_terminal;
}

bool wants_user(Protocol p) noexcept {
    return is_ssh(p) || p == Protocol::rexec || p == Protocol::rsh || p == Protocol::rdp;
}

bool is_terminal(Protocol p) noexcept {
    return p == Protocol::ssh_terminal || p == Protocol::telnet;
}

bool is_x11(Protocol p) noexcept {
    return p == Protocol::ssh_x11 || p == Protocol::xdmcp;
}

std::string_view session_name(SessionType t) noexcept {
    return info(t).name;
}

std::string_view session_label(SessionType t) noexcept {
    return info(t).label;
}

std::optional<SessionType> session_from_name(std::string_view name) noexcept {
    const auto* const it = std::ranges::find(sessions, name, &SessionInfo::name);
    if (it == sessions.end())
        return std::nullopt;
    return it->type;
}

bool Host::opens_windows_directly() const noexcept {
    return protocol == Protocol::direct_x || protocol == Protocol::rexec
           || protocol == Protocol::rsh || (is_terminal(protocol) && allow_x);
}

const Host* Settings::find_host(std::string_view id) const noexcept {
    const auto it = std::ranges::find(hosts, id, &Host::id);
    return it == hosts.end() ? nullptr : &*it;
}

std::expected<Settings, std::string> parse_settings(std::string_view text) {
    const auto root = json::parse(text);
    if (!root)
        return error("the settings are not valid JSON (byte {}: {})", root.error().offset,
                     root.error().message);
    return parse_document(*root);
}

std::string to_json(const Settings& s) {
    json::Object keyboard{{.key = "layout", .value = s.keyboard_layout}};
    if (!s.keyboard_variant.empty())
        keyboard.push_back({.key = "variant", .value = s.keyboard_variant});

    json::Object network;
    if (s.network.is_static) {
        network.push_back({.key = "method", .value = "static"});
        network.push_back({.key = "address", .value = s.network.address});
        if (!s.network.gateway.empty())
            network.push_back({.key = "gateway", .value = s.network.gateway});
        json::Array dns;
        for (const auto& server : s.network.dns)
            dns.emplace_back(server);
        network.push_back({.key = "dns", .value = std::move(dns)});
    } else {
        network.push_back({.key = "method", .value = "dhcp"});
    }

    json::Object device{
        {.key = "display",
         .value = json::Object{{.key = "mode", .value = s.display_mode},
                               {.key = "blank_minutes", .value = s.blank_minutes}}},
        {.key = "keyboard", .value = std::move(keyboard)},
        {.key = "network", .value = std::move(network)},
    };
    if (s.wifi != Wifi{})
        device.push_back({.key = "wifi",
                          .value = json::Object{{.key = "enabled", .value = s.wifi.enabled},
                                                {.key = "ssid", .value = s.wifi.ssid},
                                                {.key = "hidden", .value = s.wifi.hidden},
                                                {.key = "country", .value = s.wifi.country}}});
    if (!s.admin_pin.empty())
        device.push_back({.key = "admin_pin", .value = s.admin_pin});

    json::Array hosts;
    for (const auto& h : s.hosts)
        hosts.push_back(host_to_json(h));

    return json::write(json::Object{
        {.key = "schema_version", .value = schema_version},
        {.key = "device", .value = std::move(device)},
        {.key = "hosts", .value = std::move(hosts)},
    });
}

std::optional<std::string> network_problem(const Network& n) {
    if (!n.is_static)
        return std::nullopt;
    if (!is_valid_ipv4_cidr(n.address))
        return "The address must look like 192.168.1.20/24.";
    if (!n.gateway.empty() && (!is_valid_ip(n.gateway) || n.gateway.contains(':')))
        return "The gateway must be an IPv4 address.";
    if (n.dns.size() > max_dns_servers)
        return std::format("Give at most {} DNS servers.", max_dns_servers);
    if (!std::ranges::all_of(n.dns, [](const std::string& d) { return is_valid_ip(d); }))
        return "Each DNS server must be an IP address.";
    return std::nullopt;
}

std::string new_host_id() {
    std::array<unsigned char, 6> random{};
    if (getentropy(random.data(), random.size()) != 0)
        return {}; // cannot happen with a buffer this small
    std::string id = "h";
    for (const auto byte : random)
        id += std::format("{:02x}", byte);
    return id;
}

std::string remote_command(const Host& host) {
    switch (host.session) {
    case SessionType::raw:
        return host.command;
    case SessionType::xterm:
        // Its standard streams are detached, so programs it leaves
        // behind cannot hold the SSH channel open after logout.
        return std::format(
            R"(exec /bin/sh -c '{}command -v xterm >/dev/null || {{ echo "pixterm: xterm is not installed on this host" >&2; exit 127; }}; exec xterm </dev/null >/dev/null 2>&1')",
            require_display);
    case SessionType::xsession:
        // As xdm's Xsession does: ~/.xsession, else ~/.xinitrc, run if
        // executable and otherwise read by sh. One line, with no '!', so
        // that csh parses it too.
        return std::format(
            R"(exec /bin/sh -c '{}cd "$HOME" || exit 1; for f in .xsession .xinitrc; do if [ -f "$f" ]; then if [ -x "$f" ]; then exec "./$f" </dev/null >.xsession-errors 2>&1; else exec /bin/sh "$f" </dev/null >.xsession-errors 2>&1; fi; fi; done; echo "pixterm: neither ~/.xsession nor ~/.xinitrc exists on this host" >&2; exit 127')",
            require_display);
    case SessionType::custom:
        break;
    }
    // The command reaches sh as "$1", never parsed by the login shell.
    // Inside single quotes, ' is written '\'', which sh and csh both read.
    std::string quoted;
    for (const char c : host.command) {
        if (c == '\'')
            quoted += R"('\'')";
        else
            quoted += c;
    }
    return std::format(
        R"(exec /bin/sh -c '{}cd "$HOME" 2>/dev/null; exec /bin/sh -c "$1" </dev/null >.pixterm-session-errors 2>&1' pixterm '{}')",
        require_display, quoted);
}

} // namespace pixterm
