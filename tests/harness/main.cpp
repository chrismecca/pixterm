// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <cstdio>
#include <print>
#include <span>
#include <vector>

namespace pixterm::test {
namespace {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

int& failures() {
    static int count = 0;
    return count;
}

} // namespace

bool add(Case c) {
    registry().push_back(c);
    return true;
}

void fail(std::string_view check, std::string_view detail, std::source_location where) {
    ++failures();
    std::println(stderr, "{}:{}: FAIL {}{}{}", where.file_name(), where.line(), check,
                 detail.empty() ? "" : ": ", detail);
}

} // namespace pixterm::test

// With no arguments, runs every case. "--list" prints the case names;
// otherwise each argument names a case to run.
int main(int argc, char** argv) {
    using pixterm::test::failures;
    using pixterm::test::registry;
    const std::span args(argv + 1, static_cast<std::size_t>(argc - 1));

    if (args.size() == 1 && std::string_view(args[0]) == "--list") {
        for (const auto& c : registry())
            std::println("{}", c.name);
        return 0;
    }
    std::size_t ran = 0;
    for (const auto& c : registry()) {
        bool wanted = args.empty();
        for (const char* a : args)
            wanted = wanted || c.name == a;
        if (wanted) {
            c.run();
            ++ran;
        }
    }
    if (ran == 0) {
        std::println(stderr, "no test case matched");
        return 2;
    }
    return failures() == 0 ? 0 : 1;
}
