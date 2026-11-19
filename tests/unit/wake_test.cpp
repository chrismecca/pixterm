// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/wake.hpp>

#include <algorithm>

using namespace pixterm;

TEST(mac_addresses_are_read_with_colons_or_dashes) {
    const auto mac = parse_mac("00:1A:2b:3c:4D:5e");
    REQUIRE(mac.has_value());
    CHECK((*mac == MacAddress{0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e}));
    CHECK(parse_mac("00-1a-2b-3c-4d-5e").has_value());
    CHECK_EQ(normalize_mac("00-1A-2B-3C-4D-5E").value_or(""), "00:1a:2b:3c:4d:5e");
    for (const auto* bad : {"", "00:1a:2b:3c:4d", "00:1a:2b:3c:4d:5e:6f", "00:1a:2b:3c:4d:5g",
                            "001a2b3c4d5e", "0:1a:2b:3c:4d:5e", "00:1a-2b:3c:4d:5e"})
        CHECK(!parse_mac(bad).has_value());
}

TEST(a_magic_packet_is_six_ff_then_the_address_sixteen_times) {
    const MacAddress mac{0x00, 0x1a, 0x2b, 0x3c, 0x4d, 0x5e};
    const auto packet = magic_packet(mac);
    CHECK_EQ(packet.size(), 102U);
    CHECK(std::all_of(packet.begin(), packet.begin() + 6, [](auto b) { return b == 0xff; }));
    for (std::size_t i = 0; i < 16; ++i)
        CHECK(std::equal(mac.begin(), mac.end(), packet.begin() + 6 + (i * 6)));
}
