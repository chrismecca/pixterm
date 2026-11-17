// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/display_modes.hpp>

#include <array>

using namespace pixterm;

namespace {

Mode mode(unsigned w, unsigned h, unsigned hz, bool preferred = false, unsigned long id = 0) {
    return {.id = id, .width = w, .height = h, .hz = hz, .preferred = preferred};
}

} // namespace

TEST(refresh_rate_comes_from_the_timings) {
    // 1920x1080 at 148.5 MHz: 60 Hz.
    CHECK_EQ(refresh_rate({.dot_clock = 148'500'000, .h_total = 2200, .v_total = 1125}), 60U);
    CHECK_EQ(refresh_rate(
                 {.dot_clock = 148'500'000, .h_total = 2200, .v_total = 1125, .double_scan = true}),
             30U);
    CHECK_EQ(refresh_rate({.dot_clock = 1, .h_total = 0, .v_total = 1}), 0U);
}

TEST(monitor_name_comes_from_the_edid) {
    std::array<unsigned char, 128> edid{};
    // The second descriptor block names the monitor.
    const std::array<unsigned char, 18> name_block{0,   0,   0,   0xfc, 0,   'P', 'A', 'N', 'E',
                                                   'L', '-', '1', '\n', ' ', ' ', ' ', ' ', ' '};
    std::ranges::copy(name_block, edid.begin() + 72);
    CHECK_EQ(edid_monitor_name(edid), "PANEL-1");
    CHECK_EQ(edid_monitor_name(std::span(edid).first(100)), "");
}

TEST(modes_are_sorted_largest_first_without_repeats) {
    auto modes = sorted_modes({mode(1024, 768, 60, false, 1), mode(1920, 1080, 60, true, 2),
                               mode(1920, 1080, 60, false, 3), mode(1920, 1080, 50, false, 4)},
                              3);
    REQUIRE_EQ(modes.size(), 3U);
    CHECK_EQ(modes[0].width, 1920U);
    CHECK_EQ(modes[0].hz, 60U);
    CHECK_EQ(modes[0].id, 3UL); // the one in use is kept
    CHECK_EQ(modes[1].hz, 50U);
    CHECK_EQ(modes[2].width, 1024U);
}

TEST(finds_a_saved_mode) {
    Monitor m;
    m.modes = {mode(1920, 1080, 60), mode(1920, 1080, 50), mode(1280, 1024, 75)};
    CHECK(find_mode(m, "1920x1080@50") == 1U);
    CHECK(find_mode(m, "1920x1080") == 0U);
    CHECK(find_mode(m, "1920x1080@75") == 0U); // the size, at another rate
    CHECK(!find_mode(m, "800x600"));
    CHECK(!find_mode(m, "auto"));
}

TEST(automatic_is_the_preferred_mode) {
    Monitor m;
    m.modes = {mode(1920, 1080, 60), mode(1280, 1024, 60, true)};
    CHECK(automatic_mode(m) == 1U);
    // Without one, the largest mode nearest the panel's shape.
    m.modes[1].preferred = false;
    m.mm_width = 376;
    m.mm_height = 301; // 5:4
    CHECK(automatic_mode(m) == 1U);
    CHECK(!automatic_mode({}));
}

TEST(modes_have_names_and_labels) {
    CHECK_EQ(mode_name(mode(1920, 1080, 60)), "1920x1080@60");
    CHECK_EQ(mode_label(mode(1920, 1080, 60)), "1920 x 1080   60 Hz");
}
