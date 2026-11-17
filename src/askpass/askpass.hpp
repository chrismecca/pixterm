// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// What pixterm-askpass decides before it draws anything.

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace pixterm::askpass {

enum class Kind {
    secret,   ///< a password or passphrase
    host_key, ///< ssh asks whether to trust a new host key
    confirm,  ///< a yes or no question
    notice,   ///< a message that ssh takes down itself
};

/// The dialog for a prompt. ssh_askpass_prompt is $SSH_ASKPASS_PROMPT,
/// or null: "confirm" for questions, "none" for notices. Host key
/// questions arrive as ordinary prompts ending in "(yes/no...)?".
[[nodiscard]] Kind kind_of(std::string_view prompt, const char* ssh_askpass_prompt);

/// ssh's host key question, reworded for someone at a terminal.
[[nodiscard]] std::string host_key_message(std::string_view prompt);

/// A password as it is typed, in a fixed buffer that is wiped when it
/// is cleared or destroyed, so it never moves around the heap.
class Secret {
public:
    static constexpr std::size_t capacity = 256;

    Secret() = default;
    ~Secret() { clear(); }
    Secret(const Secret&) = delete;
    Secret& operator=(const Secret&) = delete;
    Secret(Secret&&) = delete;
    Secret& operator=(Secret&&) = delete;

    /// Replaces characters start to end with text, as a text field
    /// reports an edit. Returns false, changing nothing, if the result
    /// would not fit.
    [[nodiscard]] bool replace(std::size_t start, std::size_t end, std::string_view text);

    [[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }

    void clear() noexcept;

private:
    std::array<char, capacity> bytes_{};
    std::size_t size_ = 0;
};

} // namespace pixterm::askpass
