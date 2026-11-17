// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// A small unit test harness. Each TEST registers a case; CTest runs
/// each case in its own process (see pixterm_add_tests in
/// tests/CMakeLists.txt), so one failing case cannot hide another.
///
/// CHECK and CHECK_EQ record a failure and carry on; REQUIRE and
/// REQUIRE_EQ end the case.

#pragma once

#include <format>
#include <source_location>
#include <string>
#include <string_view>

namespace pixterm::test {

/// One registered test case.
struct Case {
    std::string_view name;
    void (*run)();
};

/// Registers a case. Called by TEST during static initialization.
bool add(Case c);

/// Records a failed check at the given place.
void fail(std::string_view check, std::string_view detail,
          std::source_location where = std::source_location::current());

/// Describes a value for a failure message, or "?" if it cannot be
/// formatted.
template <typename T>
std::string describe(const T& value) {
    if constexpr (std::formattable<T, char>) {
        if constexpr (std::is_convertible_v<const T&, std::string_view>)
            return std::format("\"{}\"", value);
        else
            return std::format("{}", value);
    } else {
        return "?";
    }
}

/// Compares two values; on a mismatch records a failure showing both.
template <typename A, typename B>
bool equal(const A& actual, const B& expected, std::string_view text,
           std::source_location where = std::source_location::current()) {
    if (actual == expected)
        return true;
    fail(text, std::format("got {}, expected {}", describe(actual), describe(expected)), where);
    return false;
}

} // namespace pixterm::test

#define TEST(name)                                                                                 \
    static void name();                                                                            \
    [[maybe_unused]] static const bool name##_registered = ::pixterm::test::add({#name, &(name)}); \
    static void name()

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            ::pixterm::test::fail(#expr, "");                                                      \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                 \
    ::pixterm::test::equal((actual), (expected), #actual " == " #expected)

#define REQUIRE(expr)                                                                              \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            ::pixterm::test::fail(#expr, "");                                                      \
            return;                                                                                \
        }                                                                                          \
    } while (false)

#define REQUIRE_EQ(actual, expected)                                                               \
    do {                                                                                           \
        if (!::pixterm::test::equal((actual), (expected), #actual " == " #expected))               \
            return;                                                                                \
    } while (false)
