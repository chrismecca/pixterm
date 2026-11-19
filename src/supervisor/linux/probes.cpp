// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "probes.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/wake.hpp>
#include <pixterm/xdmcp.hpp>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <memory>
#include <thread>

namespace pixterm::supervisor {

namespace {

constexpr int connect_ms = 5'000;
// While a host wakes, how often the packet goes out again and the host
// is tried again.
constexpr Duration wake_retry{5'000};

// The Wake-on-LAN packet for mac, to the local network's broadcast
// address on the discard port, as wake-up tools send it.
void send_wake(const std::string& text) {
    const auto mac = parse_mac(text);
    if (!mac)
        return;
    const UniqueFd s(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    const int on = 1;
    if (!s || ::setsockopt(s.get(), SOL_SOCKET, SO_BROADCAST, &on, sizeof on) != 0)
        return;
    constexpr std::uint16_t discard_port = 9;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(discard_port);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    const auto packet = magic_packet(*mac);
    (void)::sendto(s.get(), packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&to),
                   sizeof to);
}

struct FreeAddrinfo {
    void operator()(addrinfo* a) const noexcept { freeaddrinfo(a); }
};
using Addresses = std::unique_ptr<addrinfo, FreeAddrinfo>;

std::expected<Addresses, std::string> resolve(const std::string& address, int port, int type) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;
    addrinfo* found = nullptr;
    const int rc = getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &found);
    if (rc != 0)
        return std::unexpected(std::string(gai_strerror(rc)));
    return Addresses(found);
}

// 0 if a connection was made, otherwise why not.
int try_connect(const addrinfo& a) {
    const UniqueFd s(
        ::socket(a.ai_family, a.ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, a.ai_protocol));
    if (!s)
        return errno;
    if (::connect(s.get(), a.ai_addr, a.ai_addrlen) == 0)
        return 0;
    if (errno != EINPROGRESS)
        return errno;
    pollfd p{.fd = s.get(), .events = POLLOUT, .revents = 0};
    if (::poll(&p, 1, connect_ms) <= 0)
        return ETIMEDOUT;
    int error = 0;
    socklen_t length = sizeof error;
    if (::getsockopt(s.get(), SOL_SOCKET, SO_ERROR, &error, &length) != 0)
        return errno;
    return error;
}

} // namespace

std::string run_probe(const TcpProbe& probe) {
    const auto addresses = resolve(probe.address, probe.port, SOCK_STREAM);
    if (!addresses)
        return "E The address could not be found: " + addresses.error();
    // A sleeping host gets the packet, then more tries until it wakes.
    const bool waking = !probe.wake_mac.empty();
    const auto give_up = Clock::now() + (waking ? wake_time : Duration{0});
    int error = ETIMEDOUT;
    for (;;) {
        const auto tried = Clock::now();
        if (waking)
            send_wake(probe.wake_mac);
        for (const auto* a = addresses->get(); a != nullptr; a = a->ai_next) {
            error = try_connect(*a);
            if (error == 0)
                return "OK";
        }
        if (Clock::now() + wake_retry > give_up)
            break;
        std::this_thread::sleep_until(tried + wake_retry);
    }
    if (error == ETIMEDOUT)
        return "E The host did not answer.";
    return std::format("E {}.", std::strerror(error));
}

// Up to three queries, a second apart then longer, and the first
// answer.
std::string run_probe(const XdmcpProbe& probe) {
    const auto addresses = resolve(probe.address, probe.port, SOCK_DGRAM);
    if (!addresses)
        return "R " + addresses.error();
    const auto& a = **addresses;
    const UniqueFd s(::socket(a.ai_family, a.ai_socktype | SOCK_CLOEXEC, a.ai_protocol));
    const auto query =
        xdmcp::encode_query(probe.indirect ? xdmcp::QueryKind::indirect : xdmcp::QueryKind::direct);
    // A host being woken gets the packet before each query, and queries
    // go on, five seconds apart, for as long as it takes to wake.
    const bool waking = !probe.wake_mac.empty();
    const int attempts = waking ? static_cast<int>(wake_time / wake_retry) : 3;
    for (int attempt = 1; s && attempt <= attempts; ++attempt) {
        if (waking)
            send_wake(probe.wake_mac);
        if (::sendto(s.get(), query.data(), query.size(), 0, a.ai_addr, a.ai_addrlen) < 0)
            break;
        const auto until =
            Clock::now() + (waking ? wake_retry : Duration{std::chrono::seconds(attempt)});
        for (;;) {
            const auto left = std::chrono::ceil<Duration>(until - Clock::now()).count();
            pollfd p{.fd = s.get(), .events = POLLIN, .revents = 0};
            if (left <= 0 || ::poll(&p, 1, static_cast<int>(left)) <= 0)
                break;
            std::array<unsigned char, 1024> buffer{};
            const auto n = ::recv(s.get(), buffer.data(), buffer.size(), 0);
            if (n <= 0)
                continue;
            if (const auto reply =
                    xdmcp::parse_reply(std::span(buffer).first(static_cast<std::size_t>(n))))
                return std::format("{} {}\t{}", reply->willing ? 'W' : 'U', reply->hostname,
                                   reply->status);
        }
    }
    return "N";
}

} // namespace pixterm::supervisor
