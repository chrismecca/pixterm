// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include <pixterm/fd.hpp>
#include <pixterm/lines.hpp>
#include <pixterm/netstat.hpp>

#include <fcntl.h>

#include <charconv>
#include <format>
#include <ranges>
#include <string>

namespace pixterm {

namespace {

constexpr int x_base_port = 6000;
constexpr std::size_t max_proc_file = std::size_t{4} * 1024 * 1024;

std::optional<std::string> read_text(const std::filesystem::path& path) {
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd)
        return std::nullopt;
    auto text = read_all(fd.get(), max_proc_file);
    if (!text)
        return std::nullopt;
    return std::move(*text);
}

std::optional<unsigned> parse_hex_field(std::string_view s) {
    unsigned v = 0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v, 16);
    if (ec != std::errc{} || end != s.data() + s.size())
        return std::nullopt;
    return v;
}

// Adds the counts that are known; nothing if none is.
std::optional<int> sum_known(std::initializer_list<std::optional<int>> counts) {
    std::optional<int> total;
    for (const auto& c : counts)
        if (c)
            total = total.value_or(0) + *c;
    return total;
}

std::optional<int> tcp_clients(int display, const std::filesystem::path& proc) {
    const auto count = [&](std::string_view file) -> std::optional<int> {
        const auto text = read_text(proc / "net" / file);
        if (!text)
            return std::nullopt;
        return count_established(*text, x_base_port + display);
    };
    return sum_known({count("tcp"), count("tcp6")});
}

std::optional<int> unix_clients(int display, const std::filesystem::path& proc) {
    const auto text = read_text(proc / "net/unix");
    if (!text)
        return std::nullopt;
    return count_connected_x_sockets(*text, display);
}

} // namespace

int count_established(std::string_view proc_net_tcp, int port) {
    constexpr unsigned established = 1;
    int n = 0;
    // "sl: local_address rem_address st ...", with addresses as
    // HEXADDR:HEXPORT.
    for (const auto line : proc_net_tcp | std::views::split('\n')) {
        const auto words = split_words(std::string_view(line));
        if (words.size() < 4)
            continue;
        const auto local = words[1];
        const auto colon = local.rfind(':');
        if (colon == std::string_view::npos)
            continue;
        const auto local_port = parse_hex_field(local.substr(colon + 1));
        const auto state = parse_hex_field(words[3]);
        if (local_port && state && *state == established && std::cmp_equal(*local_port, port))
            ++n;
    }
    return n;
}

int count_connected_x_sockets(std::string_view proc_net_unix, int display) {
    constexpr unsigned connected = 3;
    const auto suffix = std::format("/.X11-unix/X{}", display);
    int n = 0;
    // "Num RefCount Protocol Flags Type St Inode Path"
    for (const auto line : proc_net_unix | std::views::split('\n')) {
        const auto words = split_words(std::string_view(line));
        if (words.size() < 8)
            continue;
        const auto state = parse_hex_field(words[5]);
        if (state && *state == connected && words[7].ends_with(suffix))
            ++n;
    }
    return n;
}

std::optional<bool> link_carrier(const std::filesystem::path& sys_class_net) {
    std::error_code ec;
    const std::filesystem::directory_iterator it(sys_class_net, ec);
    if (ec)
        return std::nullopt;
    std::optional<bool> carrier;
    for (const auto& entry : it) {
        const auto& dir = entry.path();
        // Physical devices only: bridges, tunnels and virtual pairs have
        // no device link. Type 1 is Ethernet, and Wi-Fi too. A Wi-Fi
        // interface that is down has no carrier file to read.
        if (dir.filename() == "lo" || !std::filesystem::exists(dir / "device", ec))
            continue;
        const auto type = read_text(dir / "type");
        if (!type || *type != "1\n")
            continue;
        const auto up = read_text(dir / "carrier");
        carrier = carrier.value_or(false) || (up && up->starts_with('1'));
    }
    return carrier;
}

std::optional<int> display_connections(int display, const std::filesystem::path& proc) {
    return sum_known({tcp_clients(display, proc), unix_clients(display, proc)});
}

std::optional<int> display_tcp_connections(int display, const std::filesystem::path& proc) {
    return tcp_clients(display, proc);
}

} // namespace pixterm
