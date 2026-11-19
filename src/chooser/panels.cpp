// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "panels.hpp"

#include <pixterm/validate.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>

namespace pixterm::chooser {

namespace {

std::string trimmed(std::string_view s) {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && blank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && blank(s.back()))
        s.remove_suffix(1);
    return std::string(s);
}

} // namespace

std::vector<std::string> mode_rows(const Monitor& m) {
    const auto automatic = automatic_mode(m);
    std::vector<std::string> rows;
    rows.reserve(m.modes.size() + 1);
    rows.push_back(std::format("Automatic  ({})",
                               automatic ? mode_label(m.modes.at(*automatic)) : "monitor default"));
    for (const auto& mode : m.modes)
        rows.push_back(mode_label(mode));
    return rows;
}

std::size_t mode_row(const Monitor& m, std::string_view saved) {
    if (saved == "auto")
        return 0;
    const auto found = find_mode(m, saved);
    return found ? *found + 1 : 0;
}

ModePick pick_mode(const Monitor& m, std::size_t row) {
    if (row == 0)
        return {.setting = "auto", .mode = automatic_mode(m)};
    const auto index = row - 1;
    return {.setting = mode_name(m.modes.at(index)), .mode = index};
}

std::string monitor_text(const std::optional<Monitor>& m) {
    if (!m)
        return "Monitor:  none detected";
    return std::format("Monitor:  {} on {}", m->name.empty() ? "unknown model" : m->name,
                       m->output);
}

std::string current_mode_text(const Monitor& m) {
    return std::format("Current mode:  {}",
                       m.current ? mode_label(m.modes.at(*m.current)) : "unknown");
}

std::string keep_mode_question(int seconds_left) {
    return std::format("Keep this display mode?\n\nThe previous mode returns in {} second{}.",
                       seconds_left, seconds_left == 1 ? "" : "s");
}

std::string blank_label(int minutes) {
    if (minutes <= 0)
        return "Never";
    return std::format("{} minute{}", minutes, minutes == 1 ? "" : "s");
}

std::size_t blank_row(int minutes) {
    const auto* const nearest = std::ranges::min_element(
        blank_choices, {}, [minutes](int choice) { return std::abs(choice - minutes); });
    return static_cast<std::size_t>(nearest - blank_choices.begin());
}

std::optional<std::size_t> layout_row(std::string_view code) {
    const auto* const it = std::ranges::find(keyboard_layouts, code, &KeyboardLayout::code);
    if (it == keyboard_layouts.end())
        return std::nullopt;
    return static_cast<std::size_t>(it - keyboard_layouts.begin());
}

std::string layout_line(const KeyboardLayout& layout) {
    return std::format("{:<6} {}", layout.code, layout.name);
}

std::expected<std::pair<std::string, std::string>, std::string>
read_keyboard(std::string_view layout, std::string_view variant) {
    if (!is_valid_layout(layout))
        return std::unexpected("Please choose a keyboard layout.");
    auto v = trimmed(variant);
    if (!is_valid_variant(v))
        return std::unexpected("The variant may contain lowercase letters, digits, '_' and '-'.");
    return std::pair{std::string(layout), std::move(v)};
}

NetworkForm network_form(const Network& n) {
    return {.is_static = n.is_static,
            .address = n.address,
            .gateway = n.gateway,
            .dns1 = !n.dns.empty() ? n.dns[0] : "",
            .dns2 = n.dns.size() > 1 ? n.dns[1] : ""};
}

std::expected<Network, std::string> read_network_form(const NetworkForm& form) {
    Network n;
    n.is_static = form.is_static;
    if (n.is_static) {
        n.address = trimmed(form.address);
        n.gateway = trimmed(form.gateway);
        for (const auto& dns : {form.dns1, form.dns2})
            if (auto d = trimmed(dns); !d.empty())
                n.dns.push_back(std::move(d));
    }
    if (auto problem = network_problem(n))
        return std::unexpected(std::move(*problem));
    return n;
}

} // namespace pixterm::chooser

namespace pixterm::chooser {

WifiForm wifi_form(const Wifi& w) {
    return {.enabled = w.enabled, .country = w.country, .ssid = w.ssid, .hidden = w.hidden};
}

std::expected<Wifi, std::string> read_wifi_form(const WifiForm& form, const Wifi& saved,
                                                std::string_view passphrase) {
    Wifi w{.enabled = form.enabled, .ssid = form.ssid, .hidden = form.hidden, .country = {}};
    w.country = trimmed(form.country);
    std::ranges::transform(w.country, w.country.begin(), [](char c) {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    });
    if ((w.enabled || !w.country.empty()) && !is_valid_country(w.country))
        return std::unexpected("Type the country as two letters, for example GB or US. It "
                               "decides which Wi-Fi channels may be used.");
    if ((w.enabled || !w.ssid.empty()) && !is_valid_ssid(w.ssid))
        return std::unexpected("Choose a network from the list, or type its name.");
    if (!w.enabled)
        return w;
    if (!passphrase.empty() && !is_valid_wifi_passphrase(passphrase))
        return std::unexpected(
            "The passphrase must be 8 to 63 letters, digits, spaces or punctuation.");
    if (passphrase.empty() && !(saved.enabled && saved.ssid == w.ssid))
        return std::unexpected(std::format("Type the passphrase for {}.", w.ssid));
    return w;
}

std::string wifi_row(const WifiNetwork& n) {
    int bars = 1;
    if (n.signal >= -55)
        bars = 4;
    else if (n.signal >= -65)
        bars = 3;
    else if (n.signal >= -75)
        bars = 2;
    std::string_view security = "WPA2";
    if (n.wpa2 && n.wpa3)
        security = "WPA2/WPA3";
    else if (n.wpa3)
        security = "WPA3";
    return std::format("{:<34}{:<4}  {}", n.ssid, std::string(static_cast<std::size_t>(bars), '|'),
                       security);
}

} // namespace pixterm::chooser

namespace pixterm::chooser {

TimeForm time_form(const TimeSettings& t) {
    return {.zone = t.zone,
            .ntp1 = !t.ntp.empty() ? t.ntp[0] : std::string(),
            .ntp2 = t.ntp.size() > 1 ? t.ntp[1] : std::string()};
}

std::expected<TimeSettings, std::string> read_time_form(const TimeForm& form) {
    TimeSettings t;
    if (auto zone = trimmed(form.zone); !zone.empty())
        t.zone = std::move(zone);
    if (!is_valid_time_zone(t.zone))
        return std::unexpected("Type the time zone as its region and city, for example "
                               "Europe/London or America/New_York, or UTC.");
    for (const auto& field : {form.ntp1, form.ntp2}) {
        auto server = trimmed(field);
        if (server.empty())
            continue;
        if (!is_valid_address(server))
            return std::unexpected("An NTP server must be a host name or an IP address.");
        t.ntp.push_back(std::move(server));
    }
    return t;
}

} // namespace pixterm::chooser

namespace pixterm::chooser {

std::size_t keyboard_model_row(std::string_view code) {
    const auto* const it = std::ranges::find(keyboard_models, code, &KeyboardModel::code);
    return it == keyboard_models.end() ? 0 : static_cast<std::size_t>(it - keyboard_models.begin());
}

} // namespace pixterm::chooser
