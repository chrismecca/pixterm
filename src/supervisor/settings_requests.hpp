// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The chooser's requests to change the terminal: save new settings,
/// reset them, export or import them, make the terminal's SSH key, and
/// restart. Each change is privileged, so each becomes a request to
/// pixterm-helper. One runs at a time, and only while the chooser is up.
///
/// The chooser hears "APPLIED [text]" when a request is done, and
/// "APPLY_FAILED text" when it is not.

#pragma once

#include <pixterm/settings.hpp>

#include <string>
#include <string_view>

namespace pixterm::supervisor {

class Supervisor;

class SettingsRequests {
public:
    explicit SettingsRequests(Supervisor& supervisor) : sup_(supervisor) {}

    /// Whether verb names one of these requests.
    [[nodiscard]] static bool handles(std::string_view verb) noexcept;

    [[nodiscard]] bool busy() const noexcept { return busy_; }

    void request(std::string_view verb);
    /// Scans for Wi-Fi networks; the chooser hears "APPLIED" and what
    /// was found, as scan_reply.
    void scan(std::string_view country);
    /// Forgets the SSH host key and RDP certificate kept for a saved host.
    void forget_key(std::string_view host_id);

private:
    Supervisor& sup_;
    bool busy_ = false;
    Settings before_;
    std::string message_;
    Settings candidate_;
    std::string passphrase_; ///< a new Wi-Fi passphrase, until handed on

    void applied(std::string_view text);
    void failed(std::string_view text);
    void apply_candidate();
    void apply_network();
    void apply_wifi();
    void apply_time();
    void save(const Settings& candidate);
    /// After the settings changed under the running system: apply the
    /// network (if asked) and display settings that differ from before,
    /// then tell the chooser.
    void finish(const Settings& before, std::string message, bool network_too);
    void apply_display_settings();
};

} // namespace pixterm::supervisor
