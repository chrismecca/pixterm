// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The host editor: which fields each protocol uses, and turning what
/// was typed into a host entry, with a message for the first field that
/// is wrong.

#pragma once

#include <pixterm/settings.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace pixterm::chooser {

/// The editor's fields, as typed.
struct HostForm {
    std::string name;
    std::string address;
    std::string port;
    std::string username;
    std::string domain;
    std::string device;
    std::string speed;
    std::string command;
    std::string font_server;
    std::string wake_mac;
    Protocol protocol = Protocol::ssh_x11;
    SessionType session = SessionType::xsession;
    bool key_auth = false;
    bool allow_x = false;
    bool indirect = false;
    bool local_wm = false;
};

/// Which fields mean something for a protocol and session type. The
/// others are shown greyed out.
struct EnabledFields {
    bool address = false;
    bool port = false;
    bool device = false;
    bool speed = false;
    bool username = false;
    bool domain = false;
    bool key_auth = false;
    bool allow_x = false;
    bool session = false;
    bool command = false;
    bool indirect = false;
    bool font_server = false;
    bool wake_mac = false;
    bool local_wm = false;
};

[[nodiscard]] EnabledFields enabled_fields(Protocol p, SessionType s) noexcept;

/// The editor's fields for a host, or for a new one if host is null.
[[nodiscard]] HostForm form_for(const Host* host);

/// The port field after the protocol changes to p. It follows the
/// protocol's usual port unless the user typed another one.
[[nodiscard]] std::string port_after_change(std::string_view port, Protocol p);

/// Checks the form and makes a host entry without an id. Text fields
/// are trimmed; fields the protocol does not use are dropped.
///
/// @return The host, or a sentence for the user about the first
///         field that is wrong.
[[nodiscard]] std::expected<Host, std::string> read_form(const HostForm& form);

/// The protocol as the editor's menu names it.
[[nodiscard]] std::string_view protocol_menu_label(Protocol p) noexcept;
/// The protocol as the host list names it.
[[nodiscard]] std::string_view protocol_list_label(Protocol p) noexcept;

/// A host's row in Configure's host list: name, address and protocol
/// in columns.
[[nodiscard]] std::string host_line(const Host& h);

/// Whether logging in to h needs a user name first.
[[nodiscard]] bool needs_username(const Host& h) noexcept;

} // namespace pixterm::chooser
