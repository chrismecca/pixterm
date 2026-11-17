// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/helper_protocol.hpp>
#include <pixterm/lines.hpp>

#include <string>

using namespace pixterm;

namespace {

// The bytes so far are the start of a valid message.
template <typename T>
bool needs_more(const helper::Parsed<T>& result) {
    if (result.has_value())
        return false;
    return result.error().need_more;
}

// The bytes can never become a valid message.
template <typename T>
bool malformed(const helper::Parsed<T>& result) {
    if (result.has_value())
        return false;
    return !result.error().need_more;
}

} // namespace

TEST(escaped_text_stays_on_one_line) {
    CHECK_EQ(escape_line("a\\b\nc\rd\x01"), "a\\\\b\\ncd ");
    CHECK_EQ(unescape_line("a\\\\b\\ncd "), "a\\b\ncd ");
    CHECK_EQ(unescape_line(escape_line("x\ny\\z")), "x\ny\\z");
    CHECK_EQ(unescape_line("trailing\\"), "trailing\\");
}

TEST(splits_lines_across_reads) {
    LineSplitter lines;
    CHECK(lines.feed("START h1 ").empty());
    const auto got = lines.feed("alice\nIDLE\n");
    REQUIRE_EQ(got.size(), 2U);
    CHECK_EQ(got[0], "START h1 alice");
    CHECK_EQ(got[1], "IDLE");
}

TEST(drops_an_overlong_line_whole) {
    LineSplitter lines;
    std::string big(max_line_length + 100, 'x');
    big.back() = '\n';
    CHECK(lines.feed(big.substr(0, 700)).empty());
    CHECK(lines.feed(big.substr(700)).empty());
    const auto got = lines.feed("OK\n");
    REQUIRE_EQ(got.size(), 1U);
    CHECK_EQ(got[0], "OK");
}

TEST(splits_words) {
    CHECK(split_words("START h1 alice") == (std::vector<std::string_view>{"START", "h1", "alice"}));
    CHECK(split_words("  A  B ") == (std::vector<std::string_view>{"A", "B"}));
    CHECK(split_words("").empty());
}

TEST(requests_round_trip) {
    const helper::Request network{
        .args = {"network", "static", "10.0.0.2/24", "-", "-", "-"},
        .payload = {},
    };
    const auto text = helper::format_request(network);
    REQUIRE(text.has_value());
    const auto back = helper::parse_request(*text);
    REQUIRE(back.has_value());
    CHECK(back->args == network.args);
    CHECK_EQ(back->payload, "");
    for (std::size_t n = 0; n < text->size(); ++n) {
        const auto partial = helper::parse_request(std::string_view(*text).substr(0, n));
        CHECK(needs_more(partial));
    }

    const helper::Request save{.args = {"config-save"}, .payload = "{}\n"};
    const auto with_payload = helper::format_request(save);
    REQUIRE(with_payload.has_value());
    const auto parsed = helper::parse_request(*with_payload);
    REQUIRE(parsed.has_value());
    CHECK_EQ(parsed->payload, "{}\n");
}

TEST(refuses_to_format_unusable_arguments) {
    CHECK(!helper::format_request({.args = {"net work"}, .payload = {}}));
    CHECK(!helper::format_request({.args = {"caf\xc3\xa9"}, .payload = {}}));
    CHECK(!helper::format_request({.args = {""}, .payload = {}}));
    CHECK(!helper::format_request({.args = {}, .payload = {}}));
    CHECK(!helper::format_request(
        {.args = {"x"}, .payload = std::string(helper::max_payload + 1, 'p')}));
}

TEST(rejects_malformed_requests) {
    for (const std::string_view bad : {
             std::string_view("2 x\nabc"),              // bytes past the payload
             std::string_view("01 x\n"),                // leading zero
             std::string_view("-1 x\n"),                // sign
             std::string_view("0\n"),                   // no command
             std::string_view("999999 x\n"),            // too large
             std::string_view("0 a\tb\n"),              // control character
             std::string_view("0 a\0b\n", 6),           // NUL
             std::string_view("0 1 2 3 4 5 6 7 8 9\n"), // too many arguments
             std::string_view("0 a  b\n"),              // two spaces
             std::string_view("0 a \n"),                // a trailing space
             std::string_view("0 caf\xc3\xa9\n"),       // not ASCII
         }) {
        if (!malformed(helper::parse_request(bad)))
            pixterm::test::fail("parse_request rejects", bad);
    }
    const auto too_long = helper::parse_request(std::string(helper::max_line_length + 1, '1'));
    CHECK(malformed(too_long));
}

TEST(replies_round_trip) {
    const auto ok = helper::parse_reply(helper::format_ok("hi"));
    REQUIRE(ok.has_value());
    CHECK(ok->ok);
    CHECK_EQ(ok->payload, "hi");

    const auto err = helper::parse_reply(helper::format_error("no\nway"));
    REQUIRE(err.has_value());
    CHECK(!err->ok);
    CHECK_EQ(err->message, "no\nway");

    CHECK(needs_more(helper::parse_reply("OK 2\nh")));
    CHECK(!helper::parse_reply("WAT\n"));
    CHECK(!helper::parse_reply("OK x\n"));
    CHECK(!helper::parse_reply("ERR x\nmore"));
}
