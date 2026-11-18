// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "failure_console.hpp"

#include "supervisor.hpp"

#include <pixterm/pin.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <ranges>

namespace pixterm::supervisor {

namespace {

std::string_view trim(std::string_view s) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && blank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && blank(s.back()))
        s.remove_suffix(1);
    return s;
}

} // namespace

std::vector<std::string> xorg_errors(std::string_view log, std::size_t max) {
    constexpr std::string_view marker = "(EE)";
    std::vector<std::string> errors;
    for (const auto part : std::views::split(log, '\n')) {
        std::string_view line(part.begin(), part.end());
        const auto at = line.find(marker);
        if (at == std::string_view::npos)
            continue;
        line = line.substr(at + marker.size());
        // Xorg ends some messages with another marker.
        if (line.ends_with(marker))
            line.remove_suffix(marker.size());
        line = trim(line);
        if (line.empty() || line.starts_with("Please also check the log file")
            || line.starts_with("Server terminated with error"))
            continue;
        errors.emplace_back(line);
    }
    if (errors.size() > max)
        errors.erase(errors.begin(), errors.end() - static_cast<std::ptrdiff_t>(max));
    return errors;
}

void FailureConsole::x_failed(int failures) {
    failures_ = failures;
    if (failures < failures_before_console || mode_ == Mode::pin || mode_ == Mode::busy)
        return;
    show_menu();
}

void FailureConsole::x_ready() {
    if (mode_ == Mode::hidden)
        return;
    if (mode_ == Mode::pin)
        sup_.system().console_echo(true);
    mode_ = Mode::hidden;
    // Cleared, so that it does not show between sessions.
    sup_.system().show_console("");
}

void FailureConsole::show_menu(std::string_view note) {
    mode_ = Mode::menu;
    std::string text;
    if (!note.empty())
        text = std::format("{}\n\n", note);
    text += std::format("pixterm cannot start its display.\n\n"
                        "The X server failed to start {} times in a row.",
                        failures_);
    const auto log = sup_.system().read_file(sup_.environment().runtime_dir / "Xorg.log");
    const auto errors = xorg_errors(log.value_or(""), 6);
    if (errors.empty()) {
        text += " Its log names no error.\n";
    } else {
        text += " Its last errors:\n\n";
        for (const auto& e : errors)
            text += std::format("    {}\n", e);
    }
    text += "\npixterm keeps trying. Press Enter to try again now.\n\n"
            "To restore the factory settings and restart the terminal, type reset\n"
            "and press Enter. Its hosts and settings are lost.\n";
    sup_.system().show_console(text);
}

void FailureConsole::line(std::string_view text) {
    switch (mode_) {
    case Mode::hidden:
    case Mode::busy:
        return;
    case Mode::pin: {
        sup_.system().console_echo(true);
        if (verify_pin(text, sup_.settings().admin_pin)) {
            reset();
            return;
        }
        locked_until_ = sup_.system().now() + wrong_pin_lockout;
        sup_.log("wrong administrator PIN on the console");
        show_menu("The PIN is wrong.");
        return;
    }
    case Mode::menu:
        break;
    }
    const auto word = trim(text);
    if (word.empty()) {
        sup_.retry_x_now();
    } else if (word == "reset") {
        if (sup_.settings().admin_pin.empty())
            reset();
        else
            ask_pin();
    } else {
        show_menu("That is not one of the choices.");
    }
}

void FailureConsole::ask_pin() {
    const auto now = sup_.system().now();
    if (locked_until_ && now < *locked_until_) {
        const auto seconds = std::chrono::ceil<std::chrono::seconds>(*locked_until_ - now).count();
        show_menu(std::format("After a wrong PIN, a reset waits {} more seconds.", seconds));
        return;
    }
    mode_ = Mode::pin;
    sup_.system().show_console("Administrator PIN: ");
    sup_.system().console_echo(false);
}

void FailureConsole::reset() {
    mode_ = Mode::busy;
    sup_.log("factory reset requested on the console");
    sup_.system().show_console("Restoring the factory settings...\n");
    const bool sent = sup_.system().call_helper(
        {.args = {"config-reset"}, .payload = {}}, [this](const helper::Reply& reply) {
            if (!reply.ok) {
                show_menu("The settings could not be reset: " + reply.message);
                return;
            }
            sup_.system().show_console("The factory settings are back. Restarting...\n");
            const bool restarting = sup_.system().call_helper(
                {.args = {"reboot"}, .payload = {}}, [this](const helper::Reply& r) {
                    if (!r.ok)
                        show_menu("The terminal could not be restarted: " + r.message);
                });
            if (!restarting)
                show_menu("The terminal could not be restarted.");
        });
    if (!sent)
        show_menu("The settings could not be reset: the helper is busy.");
}

} // namespace pixterm::supervisor
