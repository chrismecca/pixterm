// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "discovery.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>

namespace pixterm::chooser {

namespace {

struct FreeIfaddrs {
    void operator()(ifaddrs* list) const noexcept { freeifaddrs(list); }
};

struct FreeAddrinfo {
    void operator()(addrinfo* list) const noexcept { freeaddrinfo(list); }
};

// The socket API takes the generic address type.
const sockaddr* generic(const sockaddr_in& a) noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const sockaddr*>(&a);
}

void send_query(int fd, const sockaddr_in& to, xdmcp::QueryKind kind) {
    const auto packet = xdmcp::encode_query(kind);
    // A lost datagram is like a silent manager: nothing to report.
    (void)::sendto(fd, packet.data(), packet.size(), 0, generic(to), sizeof to);
}

} // namespace

bool FoundHosts::add(Found found) {
    const auto it = std::ranges::find(list_, found.address, &Found::address);
    if (it != list_.end()) {
        *it = std::move(found);
        return true;
    }
    if (list_.size() >= limit)
        return false;
    list_.push_back(std::move(found));
    return true;
}

std::string found_line(const Found& f) {
    const std::string_view name =
        f.reply.hostname.empty() ? std::string_view("?") : std::string_view(f.reply.hostname);
    return std::format("{:<22} {:<15} {}{}", name.substr(0, 22), f.address,
                       f.reply.willing ? "" : "(unwilling) ",
                       std::string_view(f.reply.status).substr(0, 34));
}

std::string answered_text(std::size_t count) {
    return std::format("{} host{} answered.", count, count == 1 ? "" : "s");
}

Host xdmcp_host(const Found& f) {
    Host h;
    const auto name = std::string_view(f.reply.hostname).substr(0, limits::name);
    h.name = !name.empty() && is_valid_name(name) ? std::string(name) : f.address;
    h.address = f.address;
    h.protocol = Protocol::xdmcp;
    h.port = xdmcp::port;
    return h;
}

std::expected<XdmcpSearch, std::string> XdmcpSearch::open(std::uint16_t port) {
    UniqueFd fd(::socket(AF_INET, SOCK_DGRAM, 0));
    const int on = 1;
    if (!fd || ::setsockopt(fd.get(), SOL_SOCKET, SO_BROADCAST, &on, sizeof on) != 0
        || ::fcntl(fd.get(), F_SETFD, FD_CLOEXEC) != 0
        || ::fcntl(fd.get(), F_SETFL, ::fcntl(fd.get(), F_GETFL) | O_NONBLOCK) != 0)
        return std::unexpected("The network is not available.");
    return XdmcpSearch(std::move(fd), port);
}

void XdmcpSearch::broadcast() {
    ifaddrs* raw = nullptr;
    if (getifaddrs(&raw) != 0)
        return;
    const std::unique_ptr<ifaddrs, FreeIfaddrs> list(raw);
    for (const auto* i = list.get(); i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET
            || (i->ifa_flags & IFF_UP) == 0 || (i->ifa_flags & IFF_BROADCAST) == 0
            || (i->ifa_flags & IFF_LOOPBACK) != 0 || i->ifa_broadaddr == nullptr)
            continue;
        sockaddr_in to{};
        std::memcpy(&to, i->ifa_broadaddr, sizeof to);
        to.sin_port = htons(port_);
        send_query(fd_.get(), to, xdmcp::QueryKind::broadcast);
    }
}

std::expected<void, std::string> XdmcpSearch::query(std::string_view host) {
    if (!is_valid_address(host))
        return std::unexpected("Please enter a host name or IPv4 address to query.");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* raw = nullptr;
    const std::string name(host);
    if (::getaddrinfo(name.c_str(), nullptr, &hints, &raw) != 0 || raw == nullptr)
        return std::unexpected("That host name could not be found.");
    const std::unique_ptr<addrinfo, FreeAddrinfo> list(raw);
    sockaddr_in to{};
    std::memcpy(&to, list->ai_addr, sizeof to);
    to.sin_port = htons(port_);
    send_query(fd_.get(), to, xdmcp::QueryKind::direct);
    return {};
}

std::vector<Found> XdmcpSearch::receive() {
    std::vector<Found> answers;
    std::array<unsigned char, 1024> buf{};
    for (;;) {
        sockaddr_in from{};
        socklen_t length = sizeof from;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto n = ::recvfrom(fd_.get(), buf.data(), buf.size(), 0,
                                  reinterpret_cast<sockaddr*>(&from), &length);
        if (n < 0)
            break;
        auto reply = xdmcp::parse_reply(std::span(buf).first(static_cast<std::size_t>(n)));
        if (!reply || from.sin_family != AF_INET)
            continue;
        std::array<char, INET_ADDRSTRLEN> text{};
        if (inet_ntop(AF_INET, &from.sin_addr, text.data(), text.size()) == nullptr)
            continue;
        answers.push_back({.address = text.data(), .reply = std::move(*reply)});
    }
    return answers;
}

} // namespace pixterm::chooser
