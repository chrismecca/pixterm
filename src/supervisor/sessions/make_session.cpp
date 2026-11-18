// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "sessions.hpp"

namespace pixterm::supervisor {

std::unique_ptr<Session> make_session(Supervisor& sup, const Host& host) {
    switch (host.protocol) {
    case Protocol::ssh_x11:
    case Protocol::ssh_terminal:
        return make_ssh_session(sup);
    case Protocol::xdmcp:
        return make_xdmcp_session(sup);
    case Protocol::telnet:
        return make_telnet_session(sup);
    case Protocol::direct_x:
        return make_direct_x_session(sup);
    case Protocol::rexec:
    case Protocol::rsh:
        return make_remote_exec_session(sup);
    case Protocol::vnc:
        return make_vnc_session(sup);
    case Protocol::rdp:
        return make_rdp_session(sup);
    case Protocol::serial:
        return make_serial_session(sup);
    case Protocol::tn3270:
        return make_tn3270_session(sup);
    case Protocol::tn5250:
        return make_tn5250_session(sup);
    case Protocol::drawterm:
        return make_drawterm_session(sup);
    }
    return make_ssh_session(sup);
}

} // namespace pixterm::supervisor
