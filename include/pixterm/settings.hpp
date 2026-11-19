// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The terminal's settings: the host list and the device settings
/// (display, keyboard, network, administrator PIN), and their JSON form.
///
/// Settings may come from a USB drive, so parse_settings checks every
/// field and accepts a document whole or not at all. Options that do
/// not apply to a host's protocol are dropped, so they never reach a
/// session.

#pragma once

#include <pixterm/validate.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm {

/// The version of the JSON layout this code reads and writes.
inline constexpr int schema_version = 1;

/// The longest wait before the screen blanks, in minutes.
inline constexpr int max_blank_minutes = 120;

/// The most hosts a settings document may hold.
inline constexpr std::size_t max_hosts = 64;

/// The most DNS servers a static network may name.
inline constexpr std::size_t max_dns_servers = 2;

/// How the terminal reaches a host.
enum class Protocol {
    ssh_x11,      ///< X11 programs over SSH
    xdmcp,        ///< a display manager's login; unencrypted
    ssh_terminal, ///< a text terminal over SSH
    telnet,       ///< a text terminal; unencrypted
    direct_x,     ///< the host opens windows on the terminal itself
    rexec,        ///< one command started with rexec; unencrypted
    rsh,          ///< one command started with rsh; trusts the address
    vnc,          ///< a remote desktop over RFB
    rdp,          ///< a Windows remote desktop
    serial,       ///< a console on one of the terminal's serial ports
    tn3270,       ///< an IBM mainframe's 3270 screens (TN3270E)
    tn5250,       ///< an IBM i's 5250 screens (TN5250E)
    drawterm,     ///< a Plan 9 or 9front CPU server, through drawterm
};

inline constexpr std::array all_protocols{
    Protocol::ssh_x11,  Protocol::xdmcp,  Protocol::ssh_terminal, Protocol::telnet,
    Protocol::direct_x, Protocol::rexec,  Protocol::rsh,          Protocol::vnc,
    Protocol::rdp,      Protocol::serial, Protocol::tn3270,       Protocol::tn5250,
    Protocol::drawterm,
};

/// The protocol's name in the settings file, such as "ssh-x11".
[[nodiscard]] std::string_view protocol_name(Protocol p) noexcept;
[[nodiscard]] std::optional<Protocol> protocol_from_name(std::string_view name) noexcept;
/// The usual TCP port, or 0 for a serial console.
[[nodiscard]] int default_port(Protocol p) noexcept;
/// SSH logins: a user name, key login and the password dialogs.
[[nodiscard]] bool is_ssh(Protocol p) noexcept;
/// Protocols that log in as a user: SSH, rexec, rsh and RDP.
[[nodiscard]] bool wants_user(Protocol p) noexcept;
/// Text terminal sessions: SSH terminal and Telnet.
[[nodiscard]] bool is_terminal(Protocol p) noexcept;
/// Sessions that show X11 programs and may use a font server.
[[nodiscard]] bool is_x11(Protocol p) noexcept;

/// What an ssh-x11 host runs.
enum class SessionType {
    xsession, ///< ~/.xsession or ~/.xinitrc, as xdm runs them
    xterm,    ///< one xterm, to test X11 forwarding
    custom,   ///< the host's command, run by /bin/sh
    raw,      ///< the host's command, sent unchanged (OpenVMS DCL)
};

/// The session type's name in the settings file.
[[nodiscard]] std::string_view session_name(SessionType t) noexcept;
/// The session type's description in the chooser.
[[nodiscard]] std::string_view session_label(SessionType t) noexcept;
[[nodiscard]] std::optional<SessionType> session_from_name(std::string_view name) noexcept;
/// Sessions that run the host's own command.
[[nodiscard]] constexpr bool takes_command(SessionType t) noexcept {
    return t == SessionType::custom || t == SessionType::raw;
}

enum class XdmcpMode {
    query,    ///< ask the host for a login
    indirect, ///< show the host's chooser of other hosts
};

enum class Auth {
    password,
    key, ///< the terminal's own SSH key
};

/// One entry in the chooser.
struct Host {
    std::string id;
    std::string name;
    std::string address; ///< empty for a serial console
    Protocol protocol = Protocol::ssh_x11;
    int port = 22;
    std::string username;                        ///< SSH, rexec, rsh, RDP; empty means ask
    Auth auth = Auth::password;                  ///< SSH only
    SessionType session = SessionType::xsession; ///< ssh-x11 only
    std::string command;                         ///< custom and raw sessions; rexec and rsh
    XdmcpMode xdmcp_mode = XdmcpMode::query;     ///< XDMCP only
    std::string font_server;                     ///< X11 sessions: "tcp/host:port", or empty
    bool allow_x = false;  ///< terminal hosts: their X programs may open windows here
    bool local_wm = false; ///< ssh-x11: run mwm here, for a host that sends none
    bool dec_keys = false; ///< terminals: the DEC VT220 editing keys and PF1-PF4
    std::string domain;    ///< RDP: the Windows domain, or empty
    std::string device;    ///< serial: the port, such as /dev/ttyUSB0
    int speed = 9600;      ///< serial: bits per second
    std::string wake_mac;  ///< woken first with Wake-on-LAN, if set: "00:1a:..."

    /// Whether a session lets the host open windows on the terminal
    /// over TCP (direct X).
    [[nodiscard]] bool opens_windows_directly() const noexcept;

    friend bool operator==(const Host&, const Host&) = default;
};

/// The Wi-Fi network the terminal joins, with DHCP. Its passphrase is
/// not part of the settings: pixterm-helper keeps it where only root
/// can read it.
struct Wifi {
    bool enabled = false;
    std::string ssid;    ///< the network's name
    bool hidden = false; ///< it does not announce itself: ask for it by name
    std::string country; ///< ISO 3166 code: which channels are legal

    friend bool operator==(const Wifi&, const Wifi&) = default;
};

/// The clock: the terminal has no battery for one, so it sets its clock
/// from the network.
struct TimeSettings {
    std::string zone = "UTC";     ///< IANA name, such as "Europe/London"
    std::vector<std::string> ntp; ///< up to two servers; none: DHCP's or Debian's

    friend bool operator==(const TimeSettings&, const TimeSettings&) = default;
};

/// The most NTP servers the settings may name.
inline constexpr std::size_t max_ntp_servers = 2;

/// The wired network: DHCP, or a static IPv4 address.
struct Network {
    bool is_static = false;
    std::string address; ///< "a.b.c.d/prefix"
    std::string gateway; ///< optional
    std::vector<std::string> dns;

    friend bool operator==(const Network&, const Network&) = default;
};

/// Everything the terminal keeps on its settings partition.
struct Settings {
    std::uint64_t generation = 0; ///< from the saved file's header; 0 if none
    std::string display_mode = "auto";
    int blank_minutes = 10; ///< idle time before the screen blanks; 0 is never
    std::string keyboard_layout = "us";
    std::string keyboard_variant;
    std::string keyboard_model = "pc105"; ///< XKB model: a PC, or a Sun USB keyboard
    Network network;
    Wifi wifi;
    TimeSettings time;
    std::string admin_pin; ///< a PIN hash, or empty for no PIN
    std::vector<Host> hosts;

    /// The host with the given id, or null.
    [[nodiscard]] const Host* find_host(std::string_view id) const noexcept;

    friend bool operator==(const Settings&, const Settings&) = default;
};

/// Reads a settings document (JSON, without a generation header). An
/// empty string member means the same as a missing one.
///
/// @return The settings, or a message naming the first problem found.
[[nodiscard]] std::expected<Settings, std::string> parse_settings(std::string_view text);

/// Writes settings as JSON. The generation is not part of the document.
[[nodiscard]] std::string to_json(const Settings& settings);

/// Why a network block cannot be used, as a sentence for the user, or
/// nothing if it is fine.
[[nodiscard]] std::optional<std::string> network_problem(const Network& network);

/// A fresh random host identifier.
[[nodiscard]] std::string new_host_id();

/// The command an ssh-x11 host's session sends to the host.
///
/// Preset sessions and custom commands run through /bin/sh, whatever
/// the user's login shell is, and fail with a clear message if X11
/// forwarding is off. A custom command reaches sh as data, quoted so
/// that sh and csh read it the same way. A raw command is sent
/// unchanged, for hosts with no POSIX shell.
[[nodiscard]] std::string remote_command(const Host& host);

} // namespace pixterm
