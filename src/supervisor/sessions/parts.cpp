// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "parts.hpp"

#include <pixterm/pin.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <memory>
#include <ranges>

namespace pixterm::supervisor {

namespace {

constexpr Duration probe_timeout{12'000}; // a name lookup, then connecting
constexpr Duration askpass_timeout{10 * 60 * 1'000};
constexpr int xterm_border = 2; // pixels

struct Font {
    int max_height; // the tallest screen it is used on
    std::string_view name;
    int width, height; // of a character cell, in pixels
};

constexpr std::array fonts{
    Font{.max_height = 799, .name = "6x13", .width = 6, .height = 13},
    Font{.max_height = 1049, .name = "9x15", .width = 9, .height = 15},
    Font{.max_height = large_screen_height - 1, .name = "10x20", .width = 10, .height = 20},
    Font{.max_height = 1 << 30, .name = "12x24", .width = 12, .height = 24},
};

} // namespace

std::string probe_reason(std::string_view output) {
    return output.starts_with("E ") ? std::string(output.substr(2)) : "The check failed.";
}

bool probe_host(Supervisor& sup, std::function<void(bool reachable)> done) {
    const auto& h = sup.host();
    sup.log(std::format("checking {} ({}, port {})", h.name, h.address, h.port));
    return sup.system().start_job(
        {.name = "tcp-probe",
         .task = TcpProbe{.address = h.address, .port = h.port},
         .timeout = probe_timeout,
         .secret = false},
        [&sup, done = std::move(done)](ExitStatus, std::string_view output) {
            if (sup.state() != State::probe)
                return;
            if (output == "OK") {
                done(true);
                return;
            }
            const auto& host = sup.host();
            sup.set_state(State::local);
            sup.report_error(std::format("Could not connect to {} ({}, port {}).\n\n{}", host.name,
                                         host.address, host.port, probe_reason(output)));
            sup.chooser_idle();
            done(false);
        });
}

bool ask_password(Supervisor& sup, std::string_view user,
                  std::function<void(std::optional<std::string>)> done) {
    const auto& h = sup.host();
    return sup.system().start_job(
        {.name = "pixterm-askpass",
         .task = Program{.name = "pixterm-askpass",
                         .argv = {sup.libexec("pixterm-askpass"),
                                  std::format("Password for {} on {}:", user, h.name)},
                         .env = sup.child_env({"PIXTERM_HOST_NAME=" + h.name}),
                         .input = {},
                         .hold_input = false},
         .timeout = askpass_timeout,
         .secret = true},
        [&sup, done = std::move(done)](ExitStatus status, std::string_view output) {
            if (sup.state() != State::probe)
                return;
            if (!status.succeeded()) {
                sup.log(std::format("login to {} cancelled", sup.host().name));
                done(std::nullopt);
                return;
            }
            done(std::string(first_line(output)));
        });
}

std::optional<std::string> login_name(const Host& host, std::string_view username) {
    const auto name = username == "-" ? std::string_view(host.username) : username;
    if (!is_valid_username(name))
        return std::nullopt;
    return std::string(name);
}

std::string_view first_line(std::string_view text) {
    return text.substr(0, text.find('\n'));
}

std::string paragraph(std::string_view text) {
    return text.empty() ? std::string() : "\n\n" + std::string(text);
}

// ---- direct X -------------------------------------------------------

std::vector<std::string> DirectX::xorg_args() {
    return {"-listen", "tcp", "-noreset"};
}

void DirectX::allow(Supervisor& sup, std::function<void()> next) {
    const bool started = sup.run_xsetup(
        {"--allow-host", sup.host().address},
        [this, &sup, next = std::move(next)](ExitStatus status, std::string_view output) {
            if (sup.state() != State::x_setup)
                return;
            // pixterm-xsetup exits with 1 when the display mode could not
            // be set, which does not matter here.
            if (!status.succeeded() && status.code != 1) {
                sup.report_error(
                    std::format("{} could not be allowed to open windows on this terminal.{}",
                                sup.host().name, paragraph(output)));
                sup.teardown();
                return;
            }
            display_.clear();
            if (const auto at = output.find("DISPLAY "); at != std::string_view::npos)
                display_ = first_line(output.substr(at + 8));
            sup.log(std::format("{} may open windows on {}", sup.host().name,
                                display_.empty() ? "this terminal" : display_));
            next();
        });
    if (!started) {
        sup.report_error(
            std::format("Internal error: cannot prepare the display for {}.", sup.host().name));
        sup.teardown();
    }
}

// ---- the terminal window --------------------------------------------

std::pair<int, int> screen_size(ExitStatus status, std::string_view output) {
    int w = 0;
    int h = 0;
    const auto space = output.find(' ');
    if (status.succeeded() && space != std::string_view::npos) {
        const auto first = output.substr(0, space);
        const auto second = first_line(output.substr(space + 1));
        std::from_chars(std::to_address(first.begin()), std::to_address(first.end()), w);
        std::from_chars(std::to_address(second.begin()), std::to_address(second.end()), h);
    }
    if (w < 320 || h < 200)
        return {1024, 768};
    return {w, h};
}

std::pair<std::string, std::string> TerminalWindow::layout(int width, int height) {
    const auto* font =
        std::ranges::find_if(fonts, [height](const Font& f) { return height <= f.max_height; });
    return {std::format("{}x{}+0+0", (width - 2 * xterm_border) / font->width,
                        (height - 2 * xterm_border) / font->height),
            std::string(font->name)};
}

std::pair<std::string, std::string> TerminalWindow::layout(int width, int height, int columns,
                                                           int rows) {
    const auto* font = &fonts.front();
    for (const auto& f : std::views::reverse(fonts)) {
        if ((columns * f.width) + (2 * xterm_border) <= width
            && (rows * f.height) + (2 * xterm_border) <= height) {
            font = &f;
            break;
        }
    }
    const int x = std::max(0, (width - (columns * font->width) - (2 * xterm_border)) / 2);
    const int y = std::max(0, (height - (rows * font->height) - (2 * xterm_border)) / 2);
    return {std::format("{}x{}+{}+{}", columns, rows, x, y), std::string(font->name)};
}

void TerminalWindow::open(Supervisor& sup, std::vector<std::string> command,
                          std::vector<std::string> env, std::string title) {
    command_ = std::move(command);
    env_ = std::move(env);
    title_ = title.empty() ? sup.host().name : std::format("{} - {}", sup.host().name, title);

    // pixtermd links no X library, so pixterm-xsetup readies the screen
    // and reports its size. On a display of the session's own, this
    // happens in x_setup.
    const auto sized = [this, &sup](ExitStatus status, std::string_view output) {
        if (sup.state() != State::handoff && sup.state() != State::x_setup)
            return;
        const auto [w, h] = screen_size(status, output);
        auto [geometry, font] = size_ ? layout(w, h, size_->first, size_->second) : layout(w, h);
        Program xterm{.name = "xterm",
                      .argv = {std::string(programs::xterm), "-geometry", geometry, "-fn", font,
                               "-bw", "0", "-b", "2", "+sb", "-title", title_, "-e"},
                      .env = env_,
                      .input = {},
                      .hold_input = false};
        if (!resource_name_.empty())
            xterm.argv.insert(xterm.argv.begin() + 1, {"-name", resource_name_});
        xterm.argv.insert(xterm.argv.end(), command_.begin(), command_.end());
        if (!sup.system().start(Slot::client, std::move(xterm))) {
            sup.report_error("Internal error: cannot start the terminal.");
            sup.teardown();
            return;
        }
        sup.log(
            std::format("terminal for {}: {} characters of {}", sup.host().name, geometry, font));
        sup.session_running(State::session);
    };
    if (!sup.run_xsetup({"--full-screen"}, sized))
        sized(ExitStatus::exited(1), "");
}

void terminal_exited(Supervisor& sup, ExitStatus status, std::string_view output) {
    if (sup.state() != State::session)
        return;
    const bool quick = sup.system().now() - sup.started(Slot::client) < session_start_grace;
    const std::string text(output);
    const auto report = [&sup, quick, text] {
        if (quick)
            sup.report_error(std::format("The terminal session on {} ended immediately.{}",
                                         sup.host().name, paragraph(text)));
        else
            sup.log(std::format("terminal session on {} ended", sup.host().name));
        sup.teardown();
    };
    if (status.succeeded())
        report();
    else
        sup.unless_display_reset(report);
}

// ---- the desktop window ---------------------------------------------

void run_desktop(Supervisor& sup, Program program) {
    const auto name = program.name;
    if (!sup.system().start(Slot::client, std::move(program))) {
        sup.report_error(std::format("Internal error: cannot start {}.", name));
        sup.teardown();
        return;
    }
    sup.session_running(State::session);
}

void desktop_exited(Supervisor& sup, ExitStatus status, std::string_view output,
                    const OrdinaryExit& normal) {
    if (sup.state() != State::session)
        return;
    const bool quick = sup.system().now() - sup.started(Slot::client) < session_start_grace;
    // A program stopped by a signal was stopped on purpose.
    const bool signalled = status.signal != 0;
    const bool ordinary = signalled || normal(status.code, output);
    const std::string text(output);
    const auto report = [&sup, quick, signalled, ordinary, status, text] {
        if (!ordinary || (!signalled && quick))
            sup.report_error(std::format("The session on {} ended {} (status {}).{}",
                                         sup.host().name, quick ? "immediately" : "with an error",
                                         status.code, paragraph(text)));
        else
            sup.log(std::format("session on {} ended", sup.host().name));
        sup.teardown();
    };
    if (ordinary)
        report();
    else
        sup.unless_display_reset(report);
}

} // namespace pixterm::supervisor
