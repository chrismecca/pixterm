// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The lines the chooser and pixtermd exchange on the control socket.
/// The chooser asks; pixtermd answers and tells it when to leave.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace pixterm::chooser {

/// A remote session now owns the display: the chooser must exit.
struct Handoff {};
/// The connection attempt is over; the chooser may be used again.
struct Idle {};
/// Something to show the user.
struct Error {
    std::string text;
};
/// A settings request is done. The text may be empty, or a message to
/// show, or the requested data (the terminal's public key).
struct Applied {
    std::string text;
};
/// A settings request failed, and why.
struct ApplyFailed {
    std::string text;
};

using Message = std::variant<Handoff, Idle, Error, Applied, ApplyFailed>;

/// Reads one line from pixtermd, or nothing if it is not a message.
[[nodiscard]] std::optional<Message> parse_message(std::string_view line);

/// Requests that carry no arguments.
enum class Request {
    cancel,          ///< stop connecting
    apply,           ///< save config.candidate.json from the runtime directory
    reset,           ///< return to the factory settings
    export_settings, ///< write the settings to a USB drive
    import_settings, ///< read the settings from a USB drive
    keygen,          ///< make (once) and show the terminal's SSH key
    reboot,
};

[[nodiscard]] std::string_view request_line(Request r) noexcept;

/// Asks for a session with a saved host. An empty user name means the
/// saved one, or none.
[[nodiscard]] std::string start_request(std::string_view host_id, std::string_view username);

/// Asks for an XDMCP session with a host found on the network.
[[nodiscard]] std::string query_request(std::string_view address, std::string_view name);

} // namespace pixterm::chooser
