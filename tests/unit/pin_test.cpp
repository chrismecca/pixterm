// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/pin.hpp>
#include <pixterm/validate.hpp>

#include <array>
#include <string>

using namespace pixterm;

TEST(a_pin_is_four_to_twelve_digits) {
    CHECK(is_valid_pin("1234"));
    CHECK(is_valid_pin("123456789012"));
    CHECK(!is_valid_pin("123"));
    CHECK(!is_valid_pin("1234567890123"));
    CHECK(!is_valid_pin("12a4"));
    CHECK(!is_valid_pin(""));
}

TEST(hashes_verify_and_never_hold_the_pin) {
    const auto stored = make_pin_hash("2468");
    REQUIRE(stored.has_value());
    CHECK(is_valid_pin_hash(*stored));
    CHECK(!stored->contains("2468"));
    CHECK(verify_pin("2468", *stored));
    CHECK(!verify_pin("2469", *stored));
    CHECK(!verify_pin("24680", *stored));
    CHECK(!verify_pin("", *stored));
}

TEST(each_hash_has_a_fresh_salt) {
    CHECK(make_pin_hash("2468") != make_pin_hash("2468"));
}

TEST(invalid_pins_are_not_hashed) {
    CHECK(!make_pin_hash("12"));
}

TEST(verifies_a_hash_made_elsewhere) {
    // From Python: hashlib.pbkdf2_hmac("sha256", b"2468", salt, 1000).
    const std::string stored = "pbkdf2-sha256$1000$00112233445566778899aabbccddeeff$"
                               "23ad2daa4e59be477bd7ef0eb0dc22c3e4f52a97976a7824b1a17a910ff6bc66";
    CHECK(verify_pin("2468", stored));
    CHECK(!verify_pin("2468", "not a hash"));
}

TEST(wipe_clears_memory) {
    std::array<char, 32> secret{};
    secret.fill('x');
    wipe(secret);
    for (const char c : secret)
        CHECK_EQ(c, '\0');
}
