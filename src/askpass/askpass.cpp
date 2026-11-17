// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "askpass.hpp"

#include <pixterm/pin.hpp>

#include <algorithm>
#include <cstring>

namespace pixterm::askpass {

Kind kind_of(std::string_view prompt, const char* ssh_askpass_prompt) {
    const std::string_view kind = ssh_askpass_prompt != nullptr ? ssh_askpass_prompt : "";
    if (kind == "confirm")
        return Kind::confirm;
    if (kind == "none")
        return Kind::notice;
    if (prompt.contains("(yes/no"))
        return Kind::host_key;
    return Kind::secret;
}

std::string host_key_message(std::string_view prompt) {
    if (const auto q = prompt.find("Are you sure you want to continue");
        q != std::string_view::npos)
        prompt = prompt.substr(0, q);
    while (!prompt.empty() && (prompt.back() == '\n' || prompt.back() == ' '))
        prompt.remove_suffix(1);
    return std::string(prompt) + "\n\nDo you trust this host and want to continue connecting?";
}

bool Secret::replace(std::size_t start, std::size_t end, std::string_view text) {
    end = std::min(end, size_);
    start = std::min(start, end);
    const std::size_t new_size = size_ - (end - start) + text.size();
    if (new_size >= capacity)
        return false;
    std::memmove(bytes_.data() + start + text.size(), bytes_.data() + end, size_ - end);
    std::ranges::copy(text, bytes_.begin() + static_cast<std::ptrdiff_t>(start));
    size_ = new_size;
    return true;
}

void Secret::clear() noexcept {
    wipe(bytes_);
    size_ = 0;
}

} // namespace pixterm::askpass
