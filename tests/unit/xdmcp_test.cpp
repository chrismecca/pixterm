// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/xdmcp.hpp>

#include <vector>

using namespace pixterm;

namespace {

// Willing: no authentication, host "xdmhost", status "load 0.5" and a
// control byte.
const std::vector<unsigned char> willing{0,   1,   0,   5,   0,   22,  0,   0, 0, 7,
                                         'x', 'd', 'm', 'h', 'o', 's', 't', 0, 9, 'l',
                                         'o', 'a', 'd', ' ', '0', '.', '5', 1};

} // namespace

TEST(encodes_queries_without_authentication) {
    using xdmcp::QueryKind;
    CHECK(xdmcp::encode_query(QueryKind::direct)
          == (std::vector<unsigned char>{0, 1, 0, 2, 0, 1, 0}));
    CHECK_EQ(xdmcp::encode_query(QueryKind::broadcast)[3], 1);
    CHECK_EQ(xdmcp::encode_query(QueryKind::indirect)[3], 3);
}

TEST(reads_willing_and_cleans_its_text) {
    const auto r = xdmcp::parse_reply(willing);
    REQUIRE(r.has_value());
    CHECK(r->willing);
    CHECK_EQ(r->hostname, "xdmhost");
    CHECK_EQ(r->status, "load 0.5?");
}

TEST(reads_unwilling) {
    const std::vector<unsigned char> unwilling{0, 1, 0, 6, 0, 8, 0, 2, 'h', 'q', 0, 2, 'n', 'o'};
    const auto r = xdmcp::parse_reply(unwilling);
    REQUIRE(r.has_value());
    CHECK(!r->willing);
    CHECK_EQ(r->hostname, "hq");
    CHECK_EQ(r->status, "no");
}

TEST(rejects_every_truncation) {
    for (std::size_t n = 0; n < willing.size(); ++n)
        CHECK(!xdmcp::parse_reply(std::span(willing).first(n)));
}

TEST(rejects_bad_headers_and_lengths) {
    auto bad = willing;
    bad[1] = 2; // version
    CHECK(!xdmcp::parse_reply(bad));
    bad = willing;
    bad[3] = 7; // opcode
    CHECK(!xdmcp::parse_reply(bad));
    bad = willing;
    bad.push_back(0); // a byte past the body
    bad[5] = 23;
    CHECK(!xdmcp::parse_reply(bad));
    bad = willing;
    bad[9] = 200; // a name longer than the packet
    CHECK(!xdmcp::parse_reply(bad));
}

TEST(limits_and_trims_text) {
    std::vector<unsigned char> p{0, 1, 0, 6, 0, 0, 0, 200};
    p.insert(p.end(), 200, 'a');
    p.insert(p.end(), {0, 3, 'o', 'k', ' '});
    p[5] = static_cast<unsigned char>(p.size() - 6);
    const auto r = xdmcp::parse_reply(p);
    REQUIRE(r.has_value());
    CHECK_EQ(r->hostname.size(), xdmcp::max_text);
    CHECK_EQ(r->status, "ok");
}
