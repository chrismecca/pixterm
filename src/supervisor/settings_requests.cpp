// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "settings_requests.hpp"

#include "supervisor.hpp"

#include <pixterm/lines.hpp>
#include <pixterm/pin.hpp>
#include <pixterm/settings_store.hpp>
#include <pixterm/validate.hpp>
#include <pixterm/wifi.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace pixterm::supervisor {

namespace {

struct Simple {
    std::string_view verb;
    std::string_view command; ///< the helper's
};

constexpr std::array<Simple, 6> simple{{
    {.verb = "RESET", .command = "config-reset"},
    {.verb = "EXPORT", .command = "usb-export"},
    {.verb = "DIAGNOSTICS", .command = "usb-diagnostics"},
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
        } else if (verb == "DIAGNOSTICS") {
            if (reply.ok)
                applied(reply.payload);
            else
                failed("The diagnostics could not be saved.\n\n" + reply.message);
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
    // A new Wi-Fi passphrase comes in a file of its own, read once.
    const auto secret_path = sup_.environment().runtime_dir / "wifi.passphrase";
    auto secret = sup_.system().read_file(secret_path);
    sup_.system().remove_file(secret_path);
    wipe(passphrase_);
    passphrase_.clear();
    if (secret) {
        const bool valid = is_valid_wifi_passphrase(*secret);
        if (valid)
            passphrase_ = *secret;
        wipe(*secret);
        if (!valid) {
            failed("The Wi-Fi passphrase must be 8 to 63 letters, digits, spaces or punctuation.");
            return;
        }
    }
    candidate_ = std::move(*candidate);
    apply_network();
}

// The network, then Wi-Fi, then the save: if one cannot be applied, the
// settings are not saved.
void SettingsRequests::apply_network() {
    const auto& next = candidate_.network;
    if (next == sup_.settings().network) {
        apply_wifi();
        return;
    }
    sup_.log(std::format("applying network settings ({})", next.is_static ? next.address : "DHCP"));
    const auto done = [this](const helper::Reply& reply) {
        if (!reply.ok)
            failed("The network settings could not be applied.\n\n" + reply.message);
        else
            apply_wifi();
    };
    if (!sup_.system().call_helper(network_request(next), done))
        failed("The network settings could not be applied.");
}

void SettingsRequests::apply_wifi() {
    const auto& now = sup_.settings().wifi;
    const auto& next = candidate_.wifi;
    std::optional<helper::Request> wifi;
    if (next.enabled && (next != now || !passphrase_.empty())) {
        sup_.log(std::format("joining the Wi-Fi network {}", next.ssid));
        wifi = helper::Request{
            .args = {"wifi", "on", next.country, to_hex(next.ssid), next.hidden ? "1" : "0"},
            .payload = passphrase_};
    } else if (!next.enabled && now.enabled) {
        sup_.log("turning Wi-Fi off");
        wifi = helper::Request{.args = {"wifi", "off"}, .payload = {}};
    }
    wipe(passphrase_);
    passphrase_.clear();
    if (!wifi) {
        save(candidate_);
        return;
    }
    const auto done = [this](const helper::Reply& reply) {
        if (!reply.ok)
            failed("Wi-Fi could not be set up.\n\n" + reply.message);
        else
            save(candidate_);
    };
    const bool sent = sup_.system().call_helper(*wifi, done);
    wipe(wifi->payload);
    if (!sent)
        failed("Wi-Fi could not be set up.");
}

void SettingsRequests::forget_key(std::string_view host_id) {
    if (busy_ || sup_.system().helper_busy()) {
        sup_.system().send_to_chooser("APPLY_FAILED Another change is still in progress.");
        return;
    }
    const auto* host = sup_.settings().find_host(host_id);
    if (host == nullptr || host->address.empty()) {
        failed("That host is not in the list.");
        return;
    }
    busy_ = true;
    sup_.log(std::format("forgetting the host key of {}", host->name));
    const auto done = [this](const helper::Reply& reply) {
        if (reply.ok)
            applied("");
        else
            failed("The host key could not be forgotten.\n\n" + reply.message);
    };
    if (!sup_.system().call_helper(
            {.args = {"host-key-forget", host->address, std::to_string(host->port)}, .payload = {}},
            done))
        failed("The host key could not be forgotten.");
}

void SettingsRequests::scan(std::string_view country) {
    if (busy_ || sup_.system().helper_busy()) {
        sup_.system().send_to_chooser("APPLY_FAILED Another change is still in progress.");
        return;
    }
    if (!is_valid_country(country)) {
        failed("Choose the country first: it decides which Wi-Fi channels may be used.");
        return;
    }
    busy_ = true;
    const auto done = [this](const helper::Reply& reply) {
        if (reply.ok)
            applied(reply.payload);
        else
            failed("The Wi-Fi scan failed.\n\n" + reply.message);
    };
    if (!sup_.system().call_helper({.args = {"wifi-scan", std::string(country)}, .payload = {}},
                                   done))
        failed("The Wi-Fi scan failed.");
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
