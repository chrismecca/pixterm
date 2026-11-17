// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/helper_protocol.hpp>
#include <pixterm/lines.hpp>

#include <format>
#include <ranges>

namespace pixterm::helper {

namespace {

std::unexpected<ParseError> malformed(std::string message) {
    return std::unexpected(ParseError{.need_more = false, .message = std::move(message)});
}

std::unexpected<ParseError> need_more() {
    return std::unexpected(ParseError{.need_more = true, .message = {}});
}

bool is_word(std::string_view s) noexcept {
    return !s.empty() && ascii::all_of(s, [](char c) { return c > ' ' && c < 0x7f; });
}

// A decimal length with no sign, blanks or leading zeros.
std::optional<std::size_t> parse_length(std::string_view s) {
    if (s.size() > 1 && s.front() == '0')
        return std::nullopt;
    const auto n = ascii::parse_decimal<std::size_t>(s);
    if (!n || *n > max_payload)
        return std::nullopt;
    return n;
}

struct Message {
    std::string_view line;
    std::string_view payload;
};

// Splits a message into its first line and a payload of the length
// read_length finds in that line. The payload must end the bytes.
template <typename ReadLength>
Parsed<Message> split(std::string_view bytes, ReadLength read_length) {
    const auto newline = bytes.substr(0, max_line_length).find('\n');
    if (newline == std::string_view::npos)
        return bytes.size() < max_line_length ? need_more()
                                              : malformed("the first line is too long");
    const auto line = bytes.substr(0, newline);
    const auto rest = bytes.substr(newline + 1);
    const std::optional<std::size_t> length = read_length(line);
    if (!length)
        return malformed("a malformed first line");
    if (rest.size() < *length)
        return need_more();
    if (rest.size() > *length)
        return malformed("bytes after the payload");
    return Message{.line = line, .payload = rest};
}

} // namespace

std::expected<std::string, std::string> format_request(const Request& request) {
    if (request.args.empty() || request.args.size() > max_arguments)
        return std::unexpected("a request has one to eight words");
    if (request.payload.size() > max_payload)
        return std::unexpected("the payload is too large");
    auto line = std::format("{}", request.payload.size());
    for (const auto& arg : request.args) {
        if (!is_word(arg))
            return std::unexpected(std::format("unusable argument \"{}\"", escape_line(arg)));
        line += ' ';
        line += arg;
    }
    if (line.size() >= max_line_length)
        return std::unexpected("the request line is too long");
    return line + '\n' + request.payload;
}

Parsed<Request> parse_request(std::string_view bytes) {
    Request r;
    const auto message = split(bytes, [&r](std::string_view line) -> std::optional<std::size_t> {
        std::optional<std::size_t> length;
        for (const auto word : line | std::views::split(' ')) {
            const std::string_view w(word);
            if (!length) {
                length = parse_length(w);
                if (!length)
                    return std::nullopt;
            } else if (is_word(w) && r.args.size() < max_arguments) {
                r.args.emplace_back(w);
            } else {
                return std::nullopt;
            }
        }
        return r.args.empty() ? std::nullopt : length;
    });
    if (!message)
        return std::unexpected(message.error());
    r.payload = message->payload;
    return r;
}

std::string format_ok(std::string_view payload) {
    return std::format("OK {}\n{}", payload.size(), payload);
}

std::string format_error(std::string_view message) {
    return std::format("ERR {}\n", escape_line(message));
}

Parsed<Reply> parse_reply(std::string_view bytes) {
    bool ok = false;
    const auto message = split(bytes, [&ok](std::string_view line) -> std::optional<std::size_t> {
        if (line.starts_with("ERR "))
            return 0;
        ok = true;
        return line.starts_with("OK ") ? parse_length(line.substr(3)) : std::nullopt;
    });
    if (!message)
        return std::unexpected(message.error());
    if (!ok)
        return Reply{.ok = false, .message = unescape_line(message->line.substr(4)), .payload = {}};
    return Reply{.ok = true, .message = {}, .payload = std::string(message->payload)};
}

} // namespace pixterm::helper
