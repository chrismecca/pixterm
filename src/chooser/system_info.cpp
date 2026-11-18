// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "system_info.hpp"

#include <pixterm/fd.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <vector>

namespace pixterm::chooser {

namespace {

struct FreeIfaddrs {
    void operator()(ifaddrs* list) const noexcept { freeifaddrs(list); }
};

// Files read here are a few lines long.
constexpr std::size_t max_small_file = std::size_t{64} * 1024;

} // namespace

namespace {

// The first IPv4 address of an interface that is up and not loopback,
// on the interface named, or any if the name is empty.
std::optional<Address> first_address(std::string_view only) {
    ifaddrs* raw = nullptr;
    if (getifaddrs(&raw) != 0)
        return std::nullopt;
    const std::unique_ptr<ifaddrs, FreeIfaddrs> list(raw);
    for (const auto* i = list.get(); i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET
            || (i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_LOOPBACK) != 0
            || (!only.empty() && only != i->ifa_name))
            continue;
        sockaddr_in sin{};
        std::memcpy(&sin, i->ifa_addr, sizeof sin);
        std::array<char, INET_ADDRSTRLEN> text{};
        if (inet_ntop(AF_INET, &sin.sin_addr, text.data(), text.size()) != nullptr)
            return Address{.interface = i->ifa_name, .ipv4 = text.data()};
    }
    return std::nullopt;
}

} // namespace

std::optional<Address> primary_address() {
    return first_address("");
}

std::optional<Address> interface_address(std::string_view interface) {
    return interface.empty() ? std::nullopt : first_address(interface);
}

std::optional<std::string> wifi_interface() {
    std::vector<std::string> found;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/net", ec))
        if (std::filesystem::exists(entry.path() / "wireless", ec))
            found.push_back(entry.path().filename().string());
    if (found.empty())
        return std::nullopt;
    std::ranges::sort(found);
    return found.front();
}

std::string network_status(const std::optional<Address>& address) {
    return std::format("Network: {}", address ? address->ipv4 : "Not connected");
}

std::string os_name(std::string_view os_release) {
    constexpr std::string_view key = "PRETTY_NAME=";
    while (!os_release.empty()) {
        const auto end = os_release.find('\n');
        auto line = os_release.substr(0, end);
        os_release = end == std::string_view::npos ? "" : os_release.substr(end + 1);
        if (!line.starts_with(key))
            continue;
        line.remove_prefix(key.size());
        if (line.size() >= 2 && line.front() == '"' && line.back() == '"')
            line = line.substr(1, line.size() - 2);
        return std::string(line);
    }
    return {};
}

std::string uptime_text(std::string_view proc_uptime) {
    long seconds = 0;
    const auto* const end = proc_uptime.data() + proc_uptime.size();
    const auto [ptr, ec] =
        std::from_chars(proc_uptime.data(), proc_uptime.data() + proc_uptime.size(), seconds);
    if (ec != std::errc{} || seconds < 0 || (ptr != end && *ptr != '.' && *ptr != ' '))
        return {};
    constexpr long day = 86400;
    const long days = seconds / day;
    return std::format("{} day{}, {}:{:02}", days, days == 1 ? "" : "s", (seconds % day) / 3600,
                       (seconds % 3600) / 60);
}

std::string version_text(std::string_view version, std::string_view build_id) {
    if (build_id.empty())
        return std::string(version);
    return std::format("{} ({})", version, build_id);
}

std::string settings_text(std::uint64_t generation) {
    if (generation == 0)
        return "factory defaults";
    return std::format("generation {}", generation);
}

std::string small_file(const std::filesystem::path& path) {
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd)
        return {};
    return read_all(fd.get(), max_small_file).value_or("");
}

std::string first_line(const std::filesystem::path& path) {
    auto text = small_file(path);
    text = text.substr(0, text.find('\n'));
    while (!text.empty() && text.back() == '\0')
        text.pop_back();
    return text;
}

} // namespace pixterm::chooser
