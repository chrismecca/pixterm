// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "settings_requests.hpp"

#include "supervisor.hpp"

#include <pixterm/lines.hpp>
#include <pixterm/settings_store.hpp>

#include <algorithm>
#include <array>
#include <format>

namespace pixterm::supervisor {

namespace {

struct Simple {
    std::string_view verb;
    std::string_view command; ///< the helper's
};

constexpr std::array<Simple, 5> simple{{
    {.verb = "RESET", .command = "config-reset"},
    {.verb = "EXPORT", .command = "usb-export"},
    {.verb = "IMPORT", .command = "usb-import"},
    {.verb = "KEYGEN", .command = "keygen"},
    {.verb = "REBOOT", .command = "reboot"},
}};

helper::Request network_request(const Network& n) {
    if (!n.is_static)
        return {.args = {"network", "dhcp"}, .payload = {}};
    const auto or_dash = [](const std::string& s) { return s.empty() ? std::string("-") : s; };
    return {.args = {"network", "static", n.address, or_dash(n.gateway),
                     or_dash(n.dns.empty() ? "" : n.dns[0]),
                     or_dash(n.dns.size() < 2 ? "" : n.dns[1])},
            .payload = {}};
}

bool display_differs(const Settings& a, const Settings& b) {
    return a.display_mode != b.display_mode || a.blank_minutes != b.blank_minutes
           || a.keyboard_layout != b.keyboard_layout || a.keyboard_variant != b.keyboard_variant;
}

} // namespace

bool SettingsRequests::handles(std::string_view verb) noexcept {
    return verb == "APPLY" || std::ranges::find(simple, verb, &Simple::verb) != simple.end();
}

void SettingsRequests::applied(std::string_view text) {
    busy_ = false;
    sup_.system().send_to_chooser(text.empty() ? std::string("APPLIED")
                                               : "APPLIED " + escape_line(text));
}

void SettingsRequests::failed(std::string_view text) {
    busy_ = false;
    sup_.log(std::format("request failed: {}", text));
    sup_.system().send_to_chooser("APPLY_FAILED " + escape_line(text));
}

void SettingsRequests::request(std::string_view verb) {
    if (busy_ || sup_.system().helper_busy()) {
        // The change in progress carries on; only this one is refused.
        sup_.system().send_to_chooser("APPLY_FAILED Another change is still in progress.");
        return;
    }
    busy_ = true;
    if (verb == "APPLY") {
        apply_candidate();
        return;
    }
    const auto* it = std::ranges::find(simple, verb, &Simple::verb);
    if (it == simple.end()) {
        busy_ = false;
        return;
    }
    sup_.log(std::format("{} requested", it->command));
    const auto done = [this, verb = it->verb](const helper::Reply& reply) {
        const Settings before = sup_.settings();
        if (verb == "REBOOT") {
            if (!reply.ok)
                failed("The terminal could not be restarted.\n\n" + reply.message);
        } else if (verb == "KEYGEN") {
            if (reply.ok)
                applied("");
            else
                failed("The terminal key could not be made.\n\n" + reply.message);
        } else if (verb == "EXPORT") {
            if (reply.ok)
                applied(reply.payload);
            else
                failed("The settings could not be exported.\n\n" + reply.message);
        } else if (!reply.ok) {
            failed(std::format("The settings could not be {}.\n\n{}",
                               verb == "RESET" ? "reset" : "imported", reply.message));
        } else {
            sup_.reload_settings();
            finish(before, verb == "IMPORT" ? "The settings were imported from the USB drive." : "",
                   true);
        }
    };
    if (!sup_.system().call_helper({.args = {std::string(it->command)}, .payload = {}}, done))
        failed("The request could not be sent.");
}

// The chooser wrote the new settings to a file in the runtime
// directory. They are checked here again: the supervisor trusts only
// what it read itself.
void SettingsRequests::apply_candidate() {
    const auto path = sup_.environment().runtime_dir / "config.candidate.json";
    const auto text = sup_.system().read_file(path);
    sup_.system().remove_file(path);
    if (!text) {
        failed("The new settings are not valid.\n\nNo settings were given.");
        return;
    }
    auto candidate = parse_saved_settings(*text);
    if (!candidate) {
        failed("The new settings are not valid.\n\n" + candidate.error());
        return;
    }
    sup_.reload_settings();
    if (candidate->network == sup_.settings().network) {
        save(*candidate);
        return;
    }
    // The network first: if it cannot be applied, nothing changes.
    sup_.log(std::format("applying network settings ({})",
                         candidate->network.is_static ? candidate->network.address : "DHCP"));
    const auto done = [this, c = *candidate](const helper::Reply& reply) {
        if (!reply.ok)
            failed("The network settings could not be applied.\n\n" + reply.message);
        else
            save(c);
    };
    if (!sup_.system().call_helper(network_request(candidate->network), done))
        failed("The network settings could not be applied.");
}

void SettingsRequests::save(const Settings& candidate) {
    const auto done = [this](const helper::Reply& reply) {
        if (!reply.ok) {
            failed("The settings could not be saved.\n\n" + reply.message);
            return;
        }
        const Settings before = sup_.settings();
        sup_.reload_settings();
        sup_.log(std::format("settings generation {} saved", sup_.settings().generation));
        finish(before, "", false);
    };
    if (!sup_.system().call_helper({.args = {"config-save"}, .payload = to_json(candidate)}, done))
        failed("The settings could not be saved.");
}

void SettingsRequests::finish(const Settings& before, std::string message, bool network_too) {
    before_ = before;
    message_ = std::move(message);
    const auto& now = sup_.settings();
    if (!network_too || before.network == now.network) {
        apply_display_settings();
        return;
    }
    const auto done = [this](const helper::Reply& reply) {
        if (!reply.ok)
            message_ += std::format("{}The network settings could not be applied: {}",
                                    message_.empty() ? "" : "\n\n", reply.message);
        apply_display_settings();
    };
    if (!sup_.system().call_helper(network_request(now.network), done))
        apply_display_settings();
}

void SettingsRequests::apply_display_settings() {
    if (sup_.state() == State::local && display_differs(before_, sup_.settings())
        && sup_.run_xsetup({}, [this](ExitStatus status, std::string_view) {
               if (!status.succeeded())
                   sup_.log("pixterm-xsetup failed");
               applied(message_);
           }))
        return;
    applied(message_);
}

} // namespace pixterm::supervisor
