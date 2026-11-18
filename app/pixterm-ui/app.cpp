// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ui.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/settings_store.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <Xm/MwmUtil.h>
#include <Xm/SelectioB.h>
#include <Xm/TextF.h>

#include <array>
#include <cstdio>
#include <format>
#include <variant>

namespace pixterm::ui {

namespace {

template <typename... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

// The most read from pixtermd at a time.
constexpr std::size_t read_size = 4096;

} // namespace

App::App(XtAppContext context, Widget shell, Paths paths, int control_fd)
    : context(context), shell(shell), paths_(std::move(paths)), control_fd_(control_fd) {
    reload_settings();
}

void App::reload_settings() {
    auto loaded = SettingsStore(paths_.state_dir, paths_.factory_settings).load();
    settings_ = std::move(loaded.settings);
    if (!loaded.problem.empty())
        error(std::format("The settings could not be loaded:\n\n{}", loaded.problem));
}

void App::error(std::string_view text) const {
    show_error(shell, text);
}

bool App::send(std::string_view line) const {
    if (control_fd_ < 0 || !send_line(control_fd_, line)) {
        error("The terminal supervisor is not responding.");
        return false;
    }
    return true;
}

namespace {

// Written whole, then renamed, so pixtermd never reads half a file. The
// runtime directory is the pixterm account's alone, in memory.
bool write_for_pixtermd(const std::filesystem::path& path, std::string_view text) {
    auto temporary = path;
    temporary += ".tmp";
    const auto written = [&] {
        const UniqueFd fd(
            ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
        return fd && write_all(fd.get(), text).has_value() && ::fsync(fd.get()) == 0;
    }();
    if (!written || std::rename(temporary.c_str(), path.c_str()) != 0) {
        (void)::unlink(temporary.c_str());
        return false;
    }
    return true;
}

} // namespace

bool App::apply(const Settings& next, std::string_view wifi_passphrase) const {
    if (paths_.runtime_dir.empty()) {
        error("Settings cannot be saved: not started by pixtermd.");
        return false;
    }
    if (!wifi_passphrase.empty()
        && !write_for_pixtermd(paths_.runtime_dir / "wifi.passphrase", wifi_passphrase)) {
        error("The Wi-Fi passphrase could not be handed on.");
        return false;
    }
    if (!write_for_pixtermd(paths_.runtime_dir / "config.candidate.json", to_json(next))) {
        error("The settings could not be written.");
        return false;
    }
    return send(chooser::request_line(chooser::Request::apply));
}

void App::read_control() {
    std::array<char, read_size> buf{};
    const auto n = ::read(control_fd_, buf.data(), buf.size());
    if (n <= 0) {
        if (n < 0 && (errno == EINTR || errno == EAGAIN))
            return;
        // pixtermd went away: nothing here can work without it.
        exit_status = 1;
        XtAppSetExitFlag(context);
        return;
    }
    for (const auto& line : lines_.feed({buf.data(), static_cast<std::size_t>(n)}))
        if (const auto message = chooser::parse_message(line))
            handle(*message);
}

void App::handle(const chooser::Message& message) {
    std::visit(Overloaded{
                   [this](const chooser::Handoff&) {
                       // A remote session owns the display from now on.
                       XtAppSetExitFlag(context);
                   },
                   [this](const chooser::Idle&) { launcher->idle(); },
                   [this](const chooser::Error& e) { error(e.text); },
                   [this](const chooser::Applied& a) {
                       reload_settings();
                       launcher->show_hosts();
                       configure->finished(true, a.text);
                   },
                   [this](const chooser::ApplyFailed& f) {
                       reload_settings();
                       launcher->show_hosts();
                       configure->finished(false, f.text);
                   },
               },
               message);
}

// ---- the administrator PIN ----

void App::require_admin(Action then) {
    if (!gate_.needs_pin(settings_.admin_pin)) {
        then();
        return;
    }
    if (pin_prompt_ == nullptr)
        build_pin_prompt();
    after_unlock_ = std::move(then);
    XtManageChild(pin_prompt_);
}

void App::build_pin_prompt() {
    Arg args[2];
    XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
    XtSetArg(args[1], XmNautoUnmanage, False); // stays up after a wrong PIN
    std::string name("pinDialog");
    pin_prompt_ = XmCreatePromptDialog(shell, name.data(), args, 2);
    XtUnmanageChild(XmSelectionBoxGetChild(pin_prompt_, XmDIALOG_HELP_BUTTON));
    const CompoundString label("Administrator PIN:");
    XtVaSetValues(pin_prompt_, XmNselectionLabelString, label.get(), nullptr);
    XtVaSetValues(XtParent(pin_prompt_), XmNtitle, "pixterm", nullptr);
    allow_move_only(XtParent(pin_prompt_));
    Widget text = XmSelectionBoxGetChild(pin_prompt_, XmDIALOG_TEXT);
    attach_secret(text, pin_);
    on(pin_prompt_, XmNokCallback, [this] { pin_entered(); });
    on(pin_prompt_, XmNcancelCallback, [this, text] {
        clear_secret(text, pin_);
        after_unlock_ = nullptr;
        XtUnmanageChild(pin_prompt_);
    });
}

void App::pin_entered() {
    Widget text = XmSelectionBoxGetChild(pin_prompt_, XmDIALOG_TEXT);
    const auto result = gate_.enter(pin_.view(), settings_.admin_pin, chooser::Clock::now());
    clear_secret(text, pin_);
    switch (result) {
    case chooser::PinGate::Result::locked_out:
        error("Too many wrong PINs. Please wait a moment and try again.");
        return;
    case chooser::PinGate::Result::wrong:
        error("That PIN is not correct.");
        return;
    case chooser::PinGate::Result::unlocked:
        break;
    }
    XtUnmanageChild(pin_prompt_);
    auto then = std::move(after_unlock_);
    after_unlock_ = nullptr;
    if (then)
        then();
}

} // namespace pixterm::ui
