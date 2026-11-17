// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <askpass.hpp>

using namespace pixterm::askpass;

TEST(the_kind_of_dialog_follows_ssh) {
    CHECK(kind_of("alice@host's password:", nullptr) == Kind::secret);
    CHECK(kind_of("Allow use of key?", "confirm") == Kind::confirm);
    CHECK(kind_of("Touch your key", "none") == Kind::notice);
    CHECK(kind_of("Are you sure you want to continue connecting (yes/no/[fingerprint])?", nullptr)
          == Kind::host_key);
}

TEST(host_key_questions_are_reworded) {
    const auto text =
        host_key_message("The authenticity of host 'desktop (10.0.0.5)' can't be established.\n"
                         "ED25519 key fingerprint is SHA256:abc.\n"
                         "Are you sure you want to continue connecting (yes/no/[fingerprint])? ");
    CHECK(text.starts_with("The authenticity of host 'desktop"));
    CHECK(text.contains("SHA256:abc."));
    CHECK(!text.contains("(yes/no"));
    CHECK(text.ends_with("\n\nDo you trust this host and want to continue connecting?"));
}

TEST(the_secret_follows_edits) {
    Secret s;
    CHECK(s.replace(0, 0, "hunter2"));
    CHECK_EQ(s.view(), "hunter2");
    CHECK(s.replace(6, 7, "3"));
    CHECK_EQ(s.view(), "hunter3");
    CHECK(s.replace(0, 6, ""));
    CHECK_EQ(s.view(), "3");
    CHECK(s.replace(5, 9, "x")); // positions past the end are clamped
    CHECK_EQ(s.view(), "3x");
}

TEST(the_secret_refuses_to_overflow) {
    Secret s;
    CHECK(s.replace(0, 0, std::string(Secret::capacity - 1, 'a')));
    CHECK(!s.replace(0, 0, "b"));
    CHECK_EQ(s.view().size(), Secret::capacity - 1);
    s.clear();
    CHECK(s.view().empty());
}
