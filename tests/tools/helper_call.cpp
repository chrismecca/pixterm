// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// helper-call SOCKET COMMAND [ARG...] [< payload]: sends one request to
// pixterm-helper and prints the raw reply. Exits 0 if the reply is OK.

#include <pixterm/fd.hpp>
#include <pixterm/helper_protocol.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <print>
#include <span>
#include <string>
#include <vector>

using namespace pixterm;

int main(int argc, char** argv) {
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() < 3) {
        std::println(stderr, "usage: helper-call SOCKET COMMAND [ARG...]");
        return 2;
    }
    helper::Request request{.args = {args.begin() + 2, args.end()}, .payload = {}};
    if (::isatty(STDIN_FILENO) == 0)
        request.payload = read_all(STDIN_FILENO, helper::max_payload).value_or("");
    const auto bytes = helper::format_request(request);
    if (!bytes) {
        std::println(stderr, "helper-call: {}", bytes.error());
        return 2;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string_view path = args[1];
    if (path.size() >= sizeof address.sun_path)
        return 2;
    std::memcpy(static_cast<char*>(address.sun_path), path.data(), path.size());
    const UniqueFd socket(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!socket
        || ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0
        || !write_all(socket.get(), *bytes)) {
        std::println(stderr, "helper-call: {}", std::strerror(errno));
        return 1;
    }
    (void)::shutdown(socket.get(), SHUT_WR);
    const auto reply = read_all(socket.get(), helper::max_payload * 2).value_or("");
    std::print("{}", reply);
    return reply.starts_with("OK") ? 0 : 1;
}
