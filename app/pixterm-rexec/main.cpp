// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// pixterm-rexec: runs one command on a host with rexec or rsh.
//
//     pixterm-rexec rexec|rsh HOST PORT USER COMMAND
//
// rexec reads the password from the first line of standard input. The
// command's output arrives on standard output. Standard input then
// stays open and is never read: the host closes the connection when the
// command exits, and that ends the session.
//
// rsh must connect from a port below 1024. The image gives this
// program, and nothing else, CAP_NET_BIND_SERVICE for that, and the
// program drops every capability once connected.
//
// Exit status: 0 the command ended, 2 the host refused, 3 no
// connection, 4 bad arguments.

#include <pixterm/fd.hpp>
#include <pixterm/pin.hpp>
#include <pixterm/validate.hpp>

#include <linux/capability.h>
#include <netdb.h>
#include <netinet/in.h>
#include <rexec.hpp>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <print>
#include <span>
#include <string>

using namespace pixterm;

namespace {

constexpr int exit_refused = 2;
constexpr int exit_no_connection = 3;
constexpr int exit_usage = 4;

struct FreeAddrinfo {
    void operator()(addrinfo* a) const noexcept { freeaddrinfo(a); }
};

void drop_capabilities() {
    __user_cap_header_struct header{.version = _LINUX_CAPABILITY_VERSION_3, .pid = 0};
    std::array<__user_cap_data_struct, 2> none{};
    (void)::syscall(SYS_capset, &header, none.data());
}

// Binds a port from 1023 down to 512, then connects.
UniqueFd connect_reserved(const addrinfo& a) {
    for (int port = 1023; port >= 512; --port) {
        UniqueFd s(::socket(a.ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (!s)
            return {};
        sockaddr_storage local{};
        local.ss_family = static_cast<sa_family_t>(a.ai_family);
        const auto p = htons(static_cast<std::uint16_t>(port));
        if (a.ai_family == AF_INET)
            reinterpret_cast<sockaddr_in*>(&local)->sin_port = p;
        else
            reinterpret_cast<sockaddr_in6*>(&local)->sin6_port = p;
        if (::bind(s.get(), reinterpret_cast<sockaddr*>(&local), a.ai_addrlen) == 0) {
            if (::connect(s.get(), a.ai_addr, a.ai_addrlen) == 0)
                return s;
            if (errno != EADDRINUSE && errno != EADDRNOTAVAIL)
                return {};
        } else if (errno == EACCES || errno == EPERM) {
            std::println(stderr, "pixterm-rexec: rsh needs a port below 1024, and this program "
                                 "may not use one");
            return {};
        }
    }
    return {};
}

UniqueFd dial(const std::string& host, const std::string& port, bool reserved) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (const int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &found); rc != 0) {
        std::println(stderr, "pixterm-rexec: {}: {}", host, gai_strerror(rc));
        return {};
    }
    const std::unique_ptr<addrinfo, FreeAddrinfo> addresses(found);
    for (const auto* a = found; a != nullptr; a = a->ai_next) {
        if (reserved) {
            if (auto s = connect_reserved(*a))
                return s;
            continue;
        }
        UniqueFd s(::socket(a->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (s && ::connect(s.get(), a->ai_addr, a->ai_addrlen) == 0)
            return s;
    }
    std::println(stderr, "pixterm-rexec: cannot connect to {}: {}", host, std::strerror(errno));
    return {};
}

} // namespace

int main(int argc, char** argv) {
    const std::span args(argv + 1, static_cast<std::size_t>(argc - 1));
    const auto kind = args.size() == 5 ? rexec::parse_kind(args[0]) : std::nullopt;
    if (!kind || !is_valid_address(args[1]) || !is_valid_username(args[3])
        || !is_valid_command(args[4])) {
        std::println(stderr, "usage: pixterm-rexec rexec|rsh HOST PORT USER COMMAND");
        return exit_usage;
    }
    const std::string host = args[1];
    std::string password;
    if (*kind == rexec::Kind::rexec)
        std::getline(std::cin, password);

    const auto s = dial(host, args[2], *kind == rexec::Kind::rsh);
    drop_capabilities();
    if (!s) {
        wipe(password);
        return exit_no_connection;
    }
    auto request = rexec::request(*kind, args[3], password, args[4]);
    const auto sent = write_all(s.get(), request);
    wipe(request);
    wipe(password);
    if (!sent) {
        std::println(stderr, "pixterm-rexec: the connection to {} closed", host);
        return exit_no_connection;
    }

    std::array<char, 4096> buffer{};
    const auto n = ::read(s.get(), buffer.data(), buffer.size());
    if (n <= 0) {
        std::println(stderr, "pixterm-rexec: {} closed the connection", host);
        return exit_no_connection;
    }
    const std::string_view answer(buffer.data(), static_cast<std::size_t>(n));
    if (const auto why = rexec::refusal(answer)) {
        std::println(stderr, "{}", *why);
        return exit_refused;
    }
    // The command's output, until it ends.
    (void)write_all(STDOUT_FILENO, answer.substr(1));
    for (;;) {
        const auto got = ::read(s.get(), buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0
            || !write_all(STDOUT_FILENO,
                          std::string_view(buffer.data(), static_cast<std::size_t>(got))))
            break;
    }
    return 0;
}
