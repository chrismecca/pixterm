// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/spawn.hpp>

using namespace pixterm;

TEST(a_program_output_is_read_to_its_end) {
    const auto out = run_for_output({"/bin/sh", "-c", "echo one; echo two"}, {}, 1024);
    CHECK_EQ(out.value_or("missing"), "one\ntwo\n");
}

TEST(output_is_cut_at_the_limit) {
    const auto out = run_for_output({"/bin/sh", "-c", "echo 0123456789"}, {}, 4);
    CHECK_EQ(out.value_or("missing"), "0123");
}

TEST(a_failed_program_has_no_output) {
    CHECK(!run_for_output({"/bin/sh", "-c", "echo partial; exit 3"}, {}, 1024).has_value());
    CHECK(!run_for_output({"/nonexistent/program"}, {}, 1024).has_value());
}
