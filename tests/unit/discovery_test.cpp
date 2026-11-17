// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/xdmcp.hpp>

#include <arpa/inet.h>
#include <discovery.hpp>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>

#include <array>

using namespace pixterm;
using namespace pixterm::chooser;

namespace {

Found found(std::string address, std::string hostname, bool willing = true) {
    return {.address = std::move(address),
            .reply = {.willing = willing, .hostname = std::move(hostname), .status = "load 0.5"}};
}

// A display manager on the loopback address that answers one query.
class FakeManager {
public:
    FakeManager() : fd_(::socket(AF_INET, SOCK_DGRAM, 0)) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t len = sizeof a;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
        if (fd_ && ::bind(fd_.get(), reinterpret_cast<sockaddr*>(&a), sizeof a) == 0
            && ::getsockname(fd_.get(), reinterpret_cast<sockaddr*>(&a), &len) == 0)
            port_ = ntohs(a.sin_port);
        // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    }

    [[nodiscard]] std::uint16_t port() const { return port_; }

    // Waits for a query and answers it with a Willing packet.
    std::vector<unsigned char> answer() {
        pollfd p{.fd = fd_.get(), .events = POLLIN, .revents = 0};
        if (::poll(&p, 1, 2000) != 1)
            return {};
        std::array<unsigned char, 512> buf{};
        sockaddr_in from{};
        socklen_t len = sizeof from;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
        const auto n = ::recvfrom(fd_.get(), buf.data(), buf.size(), 0,
                                  reinterpret_cast<sockaddr*>(&from), &len);
        if (n <= 0)
            return {};
        const std::array<unsigned char, 22> willing{
            0, 1, 0, 5, 0, 16, 0, 0, 0, 4, 'f', 'a', 'k', 'e', 0, 6, 'r', 'e', 'a', 'd', 'y', '!'};
        (void)::sendto(fd_.get(), willing.data(), willing.size(), 0,
                       reinterpret_cast<sockaddr*>(&from), len);
        // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
        return {buf.begin(), buf.begin() + n};
    }

private:
    UniqueFd fd_;
    std::uint16_t port_ = 0;
};

bool wait_readable(int fd) {
    pollfd p{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&p, 1, 2000) == 1;
}

} // namespace

TEST(answers_are_kept_once_per_address) {
    FoundHosts hosts;
    CHECK(hosts.add(found("192.0.2.1", "one")));
    CHECK(hosts.add(found("192.0.2.2", "two")));
    CHECK(hosts.add(found("192.0.2.1", "one again", false)));
    REQUIRE_EQ(hosts.list().size(), 2U);
    CHECK_EQ(hosts.list()[0].reply.hostname, "one again");
    CHECK(!hosts.list()[0].reply.willing);
    hosts.clear();
    CHECK(hosts.list().empty());
}

TEST(the_found_list_has_a_limit) {
    FoundHosts hosts;
    for (std::size_t i = 0; i < FoundHosts::limit; ++i)
        CHECK(hosts.add(found(std::format("192.0.2.{}", i), "h")));
    CHECK(!hosts.add(found("198.51.100.1", "h")));
    CHECK(hosts.add(found("192.0.2.0", "updated")));
}

TEST(found_hosts_are_described) {
    CHECK_EQ(found_line(found("192.0.2.1", "lab")),
             "lab                    192.0.2.1       load 0.5");
    CHECK_EQ(found_line(found("192.0.2.1", "", false)),
             "?                      192.0.2.1       (unwilling) load 0.5");
    CHECK_EQ(answered_text(1), "1 host answered.");
    CHECK_EQ(answered_text(3), "3 hosts answered.");
}

TEST(a_found_host_becomes_an_xdmcp_entry) {
    const auto h = xdmcp_host(found("192.0.2.1", "lab"));
    CHECK_EQ(h.name, "lab");
    CHECK_EQ(h.address, "192.0.2.1");
    CHECK(h.protocol == Protocol::xdmcp);
    CHECK_EQ(h.port, 177);
    // A manager with no usable name is named by its address.
    CHECK_EQ(xdmcp_host(found("192.0.2.1", "")).name, "192.0.2.1");
}

TEST(a_query_reaches_the_manager_and_its_answer_comes_back) {
    FakeManager manager;
    REQUIRE(manager.port() != 0);
    auto search = XdmcpSearch::open(manager.port());
    REQUIRE(search.has_value());
    REQUIRE(search->query("127.0.0.1").has_value());
    const auto query = manager.answer();
    CHECK(query == xdmcp::encode_query(xdmcp::QueryKind::direct));
    REQUIRE(wait_readable(search->fd()));
    const auto answers = search->receive();
    REQUIRE_EQ(answers.size(), 1U);
    CHECK_EQ(answers[0].address, "127.0.0.1");
    CHECK_EQ(answers[0].reply.hostname, "fake");
    CHECK_EQ(answers[0].reply.status, "ready!");
    CHECK(search->receive().empty());
}

TEST(a_bad_query_address_is_refused) {
    auto search = XdmcpSearch::open(xdmcp::port);
    REQUIRE(search.has_value());
    CHECK(!search->query("-x").has_value());
    CHECK(!search->query("no.such.host.invalid").has_value());
}
