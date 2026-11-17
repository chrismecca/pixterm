// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/json.hpp>

#include <string>

using namespace pixterm;

TEST(parses_scalars) {
    CHECK(json::parse("null").value().is_null());
    CHECK_EQ(json::parse("true").value().as_bool(), std::optional(true));
    CHECK_EQ(json::parse(" false ").value().as_bool(), std::optional(false));
    CHECK_EQ(json::parse("\"hi\"").value().as_string(), std::optional<std::string_view>("hi"));
    CHECK_EQ(json::parse("-12.5e1").value().as_number(), std::optional(-125.0));
}

TEST(integers_must_be_whole_numbers) {
    CHECK_EQ(json::parse("22").value().as_integer(), std::optional<std::int64_t>(22));
    CHECK_EQ(json::parse("-1").value().as_integer(), std::optional<std::int64_t>(-1));
    CHECK_EQ(json::parse("2e3").value().as_integer(), std::optional<std::int64_t>(2000));
    CHECK(!json::parse("22.5").value().as_integer());
    CHECK(!json::parse("1e300").value().as_integer());
    CHECK(!json::parse("\"22\"").value().as_integer());
}

TEST(parses_objects_in_order) {
    const auto v = json::parse(R"({"b": 1, "a": [true, null], "c": {}})");
    REQUIRE(v.has_value());
    const auto* object = v->as_object();
    REQUIRE(object != nullptr);
    REQUIRE_EQ(object->size(), 3U);
    CHECK_EQ((*object)[0].key, "b");
    CHECK_EQ((*object)[1].key, "a");
    CHECK_EQ(v->find("a")->as_array()->size(), 2U);
    CHECK(v->find("missing") == nullptr);
    CHECK(json::Value().find("a") == nullptr);
}

TEST(decodes_string_escapes) {
    CHECK_EQ(json::parse(R"("a\"b\\c\/d\n\t")").value().as_string().value_or(""), "a\"b\\c/d\n\t");
    CHECK_EQ(json::parse(R"("\u00e9")").value().as_string().value_or(""), "\xc3\xa9");
    CHECK_EQ(json::parse(R"("\ud83d\ude00")").value().as_string().value_or(""), "\xf0\x9f\x98\x80");
}

TEST(rejects_malformed_text) {
    for (const char* bad :
         {"",    "{",        "[1,]",      R"({"a":1,})", "{'a':1}",     "01",
          "1.",  ".5",       "+1",        "-",           "1e",          "tru",
          "nul", R"("open)", "[1 2]",     R"({"a" 1})",  "{1:2}",       "// x\n1",
          "1 2", R"("\x")",  R"("\u12")", R"("\ud83d")", R"("\ude00")", "\"tab\there\"",
          "NaN", "Infinity"}) {
        if (json::parse(bad))
            pixterm::test::fail("parse rejects", bad);
    }
}

TEST(rejects_what_settings_must_never_hold) {
    CHECK(!json::parse(R"({"a": 1, "a": 2})")); // ambiguous duplicate keys
    CHECK(!json::parse(R"("nul\u0000")"));      // would end a C string early
    CHECK(!json::parse("\"\xff\""));            // not UTF-8
    CHECK(!json::parse("\"\xc0\xaf\""));        // overlong UTF-8
    CHECK(!json::parse("\"\xed\xa0\x80\""));    // UTF-8 surrogate
}

TEST(limits_nesting_depth) {
    CHECK(json::parse(std::string(32, '[') + std::string(32, ']')));
    CHECK(!json::parse(std::string(100, '[') + std::string(100, ']')));
}

TEST(reports_where_parsing_failed) {
    const auto v = json::parse("{\"a\": tru}");
    REQUIRE(!v);
    CHECK_EQ(v.error().offset, 6U);
    CHECK(!v.error().message.empty());
}

TEST(writes_pretty_ascii) {
    json::Object hosts;
    hosts.push_back({"name", json::Value("caf\xc3\xa9 \"one\"")});
    hosts.push_back({"port", json::Value(22)});
    hosts.push_back({"x", json::Value(true)});
    hosts.push_back({"list", json::Value(json::Array{json::Value(), json::Value(1.5)})});
    hosts.push_back({"empty", json::Value(json::Object{})});
    CHECK_EQ(json::write(json::Value(std::move(hosts))), "{\n"
                                                         "  \"name\": \"caf\\u00e9 \\\"one\\\"\",\n"
                                                         "  \"port\": 22,\n"
                                                         "  \"x\": true,\n"
                                                         "  \"list\": [\n"
                                                         "    null,\n"
                                                         "    1.5\n"
                                                         "  ],\n"
                                                         "  \"empty\": {}\n"
                                                         "}\n");
}

TEST(writes_escapes_that_read_back) {
    const std::string text = "line\nnext\x01 \xf0\x9f\x98\x80 \\ /";
    const auto again = json::parse(json::write(json::Value(text)));
    REQUIRE(again.has_value());
    CHECK_EQ(again->as_string().value_or(""), text);
}
