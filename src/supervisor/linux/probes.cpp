// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "probes.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/xdmcp.hpp>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <memory>

namespace pixterm::supervisor {

namespace {

constexpr int connect_ms = 5'000;

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
    int error = ETIMEDOUT;
    for (const auto* a = addresses->get(); a != nullptr; a = a->ai_next) {
        error = try_connect(*a);
        if (error == 0)
            return "OK";
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
    for (int attempt = 1; s && attempt <= 3; ++attempt) {
        if (::sendto(s.get(), query.data(), query.size(), 0, a.ai_addr, a.ai_addrlen) < 0)
            break;
        const auto until = Clock::now() + std::chrono::seconds(attempt);
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
