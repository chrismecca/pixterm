// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// The kinds of session, one file each.

#pragma once

#include "../supervisor.hpp"

#include <memory>

namespace pixterm::supervisor {

/// SSH with X11 forwarding, and SSH terminals.
[[nodiscard]] std::unique_ptr<Session> make_ssh_session(Supervisor& sup);
/// A display manager's login over XDMCP.
[[nodiscard]] std::unique_ptr<Session> make_xdmcp_session(Supervisor& sup);
/// Telnet terminals.
[[nodiscard]] std::unique_ptr<Session> make_telnet_session(Supervisor& sup);
/// Hosts that open their windows on the terminal themselves.
[[nodiscard]] std::unique_ptr<Session> make_direct_x_session(Supervisor& sup);
/// One command started with rexec or rsh.
[[nodiscard]] std::unique_ptr<Session> make_remote_exec_session(Supervisor& sup);
/// VNC remote desktops.
[[nodiscard]] std::unique_ptr<Session> make_vnc_session(Supervisor& sup);
/// RDP remote desktops.
[[nodiscard]] std::unique_ptr<Session> make_rdp_session(Supervisor& sup);
/// Serial consoles.
[[nodiscard]] std::unique_ptr<Session> make_serial_session(Supervisor& sup);
[[nodiscard]] std::unique_ptr<Session> make_tn3270_session(Supervisor& sup);
[[nodiscard]] std::unique_ptr<Session> make_tn5250_session(Supervisor& sup);
[[nodiscard]] std::unique_ptr<Session> make_drawterm_session(Supervisor& sup);

} // namespace pixterm::supervisor
