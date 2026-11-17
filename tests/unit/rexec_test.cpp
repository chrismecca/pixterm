// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <rexec.hpp>

#include <string>

using namespace pixterm;
using namespace std::string_literals;

TEST(rexec_sends_user_password_and_command) {
    CHECK_EQ(rexec::request(rexec::Kind::rexec, "guest", "pw", "xterm -display h:0"),
             "0\0guest\0pw\0xterm -display h:0\0"s);
}

TEST(rsh_sends_the_user_twice_and_no_password) {
    CHECK_EQ(rexec::request(rexec::Kind::rsh, "chris", "ignored", "xclock"),
             "0\0chris\0chris\0xclock\0"s);
}

TEST(reads_the_hosts_answer) {
    CHECK(rexec::refusal("\0"s) == std::nullopt);
    CHECK_EQ(rexec::refusal("\x01Login incorrect.\nmore"s).value_or(""), "Login incorrect.");
    CHECK_EQ(rexec::refusal("\x01"s).value_or(""), "The host refused the command.");
}

TEST(checks_its_arguments) {
    CHECK(rexec::parse_kind("rexec") == rexec::Kind::rexec);
    CHECK(rexec::parse_kind("rsh") == rexec::Kind::rsh);
    CHECK(!rexec::parse_kind("rlogin"));
}
