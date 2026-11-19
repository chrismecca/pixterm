// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "messages.hpp"

#include <pixterm/lines.hpp>

#include <format>

namespace pixterm::chooser {

namespace {

// The text after "VERB " in a line, or nothing if the line is not that
// verb. A bare verb gives empty text.
std::optional<std::string> argument(std::string_view line, std::string_view verb) {
    if (!line.starts_with(verb))
        return std::nullopt;
    line.remove_prefix(verb.size());
    if (line.empty())
        return std::string();
    if (line.front() != ' ')
        return std::nullopt;
    return unescape_line(line.substr(1));
}

} // namespace

std::optional<Message> parse_message(std::string_view line) {
    if (line == "HANDOFF")
        return Handoff{};
    if (line == "IDLE")
        return Idle{};
    if (auto text = argument(line, "APPLY_FAILED"))
        return ApplyFailed{std::move(*text)};
    if (auto text = argument(line, "APPLIED"))
        return Applied{std::move(*text)};
    if (auto text = argument(line, "ERROR"))
        return Error{std::move(*text)};
    return std::nullopt;
}

std::string_view request_line(Request r) noexcept {
    switch (r) {
    case Request::cancel:
        return "CANCEL";
    case Request::apply:
        return "APPLY";
    case Request::reset:
        return "RESET";
    case Request::export_settings:
        return "EXPORT";
    case Request::diagnostics:
        return "DIAGNOSTICS";
    case Request::import_settings:
        return "IMPORT";
    case Request::keygen:
        return "KEYGEN";
    case Request::reboot:
        break;
    }
    return "REBOOT";
}

std::string start_request(std::string_view host_id, std::string_view username) {
    return std::format("START {} {}", host_id, username.empty() ? "-" : username);
}

std::string forget_key_request(std::string_view host_id) {
    return std::format("FORGET_KEY {}", host_id);
}

std::string wifi_scan_request(std::string_view country) {
    return std::format("WIFI_SCAN {}", country);
}

std::string query_request(std::string_view address, std::string_view name) {
    return std::format("QUERY {} {}", address, escape_line(name));
}

} // namespace pixterm::chooser
