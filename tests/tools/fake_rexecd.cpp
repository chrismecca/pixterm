// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// A stand-in rexec or rsh server for the session test (Debian no longer
// has either). It serves one connection at a time on 127.0.0.1:PORT and
// runs the command as USER, with the connection as its input and output.
//
//     fake-rexecd rexec|rsh PORT USER [PASSWORD]
//
// rexec accepts only PASSWORD; rsh accepts only a client port below
// 1024, as a real rshd does.

#include <pixterm/fd.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <charconv>
#include <csignal>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>

using namespace pixterm;

namespace {

// Reads one NUL-terminated string of at most limit bytes.
std::optional<std::string> read_string(int fd, std::size_t limit) {
    std::string s;
    char c = 0;
    while (s.size() < limit) {
        if (::read(fd, &c, 1) != 1)
            return std::nullopt;
        if (c == '\0')
            return s;
        s += c;
    }
    return std::nullopt;
}

void refuse(int fd, std::string_view why) {
    (void)write_all(fd, std::string("\1") + std::string(why) + "\n");
}

struct Server {
    bool rsh = false;
    std::string user;
    std::string password;

    void serve(int fd, const sockaddr_in& peer) const {
        const auto port = read_string(fd, 16);
        const auto first = read_string(fd, 64);
        const auto second = read_string(fd, 256);
        const auto command = read_string(fd, 1024);
        if (!port || !first || !second || !command)
            return;
        if (*port != "0") {
            refuse(fd, "no error channel, please");
            return;
        }
        if (rsh && ntohs(peer.sin_port) >= 1024) {
            refuse(fd, "permission denied: not from a reserved port");
            return;
        }
        if (*first != user || *second != (rsh ? user : password)) {
            refuse(fd, "Login incorrect");
            return;
        }
        if (!write_all(fd, std::string(1, '\0')))
            return;
        if (::fork() == 0) {
            (void)::dup2(fd, 0);
            (void)::dup2(fd, 1);
            (void)::dup2(fd, 2);
            ::execl("/usr/sbin/runuser", "runuser", "-u", user.c_str(), "--", "/bin/sh", "-c",
                    command->c_str(), nullptr);
            ::_exit(127);
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    const std::span args(argv, static_cast<std::size_t>(argc));
    const std::string_view kind = args.size() > 1 ? args[1] : "";
    std::uint16_t port = 0;
    const std::string_view port_text = args.size() > 2 ? args[2] : "";
    const auto parsed =
        std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (args.size() < 4 || (kind != "rexec" && kind != "rsh") || parsed.ec != std::errc{}) {
        std::println(stderr, "usage: fake-rexecd rexec|rsh PORT USER [PASSWORD]");
        return 2;
    }
    const Server server{
        .rsh = kind == "rsh", .user = args[3], .password = args.size() > 4 ? args[4] : ""};
    (void)std::signal(SIGCHLD, SIG_IGN);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    const UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    const int one = 1;
    if (!listener || ::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) != 0
        || ::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0
        || ::listen(listener.get(), 4) != 0) {
        std::println(stderr, "fake-rexecd: cannot listen on port {}", port);
        return 1;
    }
    for (;;) {
        sockaddr_in peer{};
        socklen_t length = sizeof peer;
        const UniqueFd connection(
            ::accept(listener.get(), reinterpret_cast<sockaddr*>(&peer), &length));
        if (connection)
            server.serve(connection.get(), peer);
    }
}
