// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/lines.hpp>

namespace pixterm {

std::string escape_line(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            continue;
        else
            out += ascii::is_control(c) ? ' ' : c;
    }
    return out;
}

std::string unescape_line(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            out += text[i] == 'n' ? '\n' : text[i];
        } else {
            out += text[i];
        }
    }
    return out;
}

std::vector<std::string_view> split_words(std::string_view line) {
    std::vector<std::string_view> words;
    while (!line.empty()) {
        const auto start = line.find_first_not_of(' ');
        if (start == std::string_view::npos)
            break;
        line.remove_prefix(start);
        const auto end = line.find(' ');
        words.push_back(line.substr(0, end));
        line.remove_prefix(end == std::string_view::npos ? line.size() : end);
    }
    return words;
}

std::vector<std::string> LineSplitter::feed(std::string_view data) {
    std::vector<std::string> lines;
    for (const char c : data) {
        if (c == '\n') {
            if (!discarding_)
                lines.push_back(std::move(partial_));
            partial_.clear();
            discarding_ = false;
        } else if (!discarding_) {
            if (partial_.size() == max_line_length) {
                partial_.clear();
                discarding_ = true;
            } else {
                partial_ += c;
            }
        }
    }
    return lines;
}

std::expected<void, std::error_code> send_line(int fd, std::string_view line) {
    if (line.size() > max_line_length || line.contains('\n'))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    std::string message(line);
    message += '\n';
    return write_all(fd, message);
}

} // namespace pixterm
