// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "rexec.hpp"

namespace pixterm::rexec {

std::optional<Kind> parse_kind(std::string_view name) noexcept {
    if (name == "rexec")
        return Kind::rexec;
    if (name == "rsh")
        return Kind::rsh;
    return std::nullopt;
}

std::string request(Kind kind, std::string_view user, std::string_view password,
                    std::string_view command) {
    std::string bytes;
    for (const auto field :
         {std::string_view("0"), user, kind == Kind::rexec ? password : user, command}) {
        bytes += field;
        bytes += '\0';
    }
    return bytes;
}

std::optional<std::string> refusal(std::string_view answer) {
    if (!answer.empty() && answer.front() == '\0')
        return std::nullopt;
    auto reason = answer.empty() ? std::string_view() : answer.substr(1);
    reason = reason.substr(0, reason.find('\n'));
    if (reason.empty())
        return "The host refused the command.";
    return std::string(reason);
}

} // namespace pixterm::rexec
