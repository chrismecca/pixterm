// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Pieces several kinds of session share.

#pragma once

#include "../supervisor.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pixterm::supervisor {

/// A session program that ended this soon after starting never really
/// ran, whatever its status.
inline constexpr Duration session_start_grace{5'000};

namespace programs {
inline constexpr std::string_view ssh = "/usr/bin/ssh";
inline constexpr std::string_view xauth = "/usr/bin/xauth";
inline constexpr std::string_view telnet = "/usr/bin/telnet";
inline constexpr std::string_view xterm = "/usr/bin/xterm";
inline constexpr std::string_view picocom = "/usr/bin/picocom";
inline constexpr std::string_view vncviewer = "/usr/bin/xtigervncviewer";
inline constexpr std::string_view xfreerdp = "/usr/bin/xfreerdp3";
inline constexpr std::string_view x3270 = "/usr/bin/x3270";
inline constexpr std::string_view tn5250 = "/usr/local/bin/tn5250";
inline constexpr std::string_view drawterm = "/usr/local/bin/drawterm";
} // namespace programs

/// The largest 3270 font ("3270gt8" to "3270gt32") that shows a whole
/// 3270 model 2 screen on a screen this size.
[[nodiscard]] std::string x3270_font(int width, int height);

/// "address:port", with an IPv6 address in brackets: "[2001:db8::1]:23".
[[nodiscard]] std::string with_port(std::string_view address, int port);

/// The address alone, in brackets if it is IPv6, for programs that add
/// the port themselves.
[[nodiscard]] std::string bracketed(std::string_view address);

/// The text after "E " in a TCP probe's failure, or a general reason.
[[nodiscard]] std::string probe_reason(std::string_view output);

/// Checks that the current host accepts TCP connections. done gets
/// true when it does; when it does not, the user has been told and the
/// chooser is idle again.
[[nodiscard]] bool probe_host(Supervisor& sup, std::function<void(bool reachable)> done);

/// Asks for a password with pixterm-askpass, as a secret job. done gets
/// the password, or nothing if the user cancelled.
[[nodiscard]] bool ask_password(Supervisor& sup, std::string_view user,
                                std::function<void(std::optional<std::string>)> done);

/// The user name for a login: the chooser's, or the host's own when
/// the chooser sent "-". Nothing if it is not valid.
[[nodiscard]] std::optional<std::string> login_name(const Host& host, std::string_view username);

/// The first line of text.
[[nodiscard]] std::string_view first_line(std::string_view text);

/// Text with a blank line before it, or nothing if it is empty.
[[nodiscard]] std::string paragraph(std::string_view text);

/// Hosts that open windows on the terminal over TCP: on the session's
/// own display, Xorg listens on TCP and only the session's host may
/// connect (pixterm-xsetup --allow-host).
class DirectX {
public:
    /// The extra arguments of a display that listens on TCP.
    [[nodiscard]] static std::vector<std::string> xorg_args();

    /// The session's own display is up: let the host in, then call next.
    void allow(Supervisor& sup, std::function<void()> next);

    /// The display name the host uses, such as "10.0.0.5:0", once known.
    [[nodiscard]] const std::string& display() const noexcept { return display_; }

private:
    std::string display_;
};

/// The window of a text session: one xterm that fills the screen and
/// runs one command, such as ssh or telnet. There is no local shell.
/// When the command ends, the xterm closes and so does the session.
class TerminalWindow {
public:
    /// Opens the terminal once the screen is ready; the session runs
    /// from then on. title follows the host's name in the window title.
    void open(Supervisor& sup, std::vector<std::string> command, std::vector<std::string> env,
              std::string title);

    /// The xterm's geometry and font for a screen, as "COLSxROWS+0+0"
    /// and a font name.
    [[nodiscard]] static std::pair<std::string, std::string> layout(int width, int height);

    /// The geometry and font for a terminal of a fixed size (columns by
    /// rows): the largest font that fits, centred.
    [[nodiscard]] static std::pair<std::string, std::string> layout(int width, int height,
                                                                    int columns, int rows);

    /// The xterm's resource name, for resources (such as a key map) of
    /// its own. Before open.
    void set_resource_name(std::string name) { resource_name_ = std::move(name); }
    /// A fixed size, columns by rows, instead of the whole screen.
    /// Before open.
    void set_size(int columns, int rows) { size_ = {columns, rows}; }

private:
    std::string resource_name_;
    std::optional<std::pair<int, int>> size_;
    std::vector<std::string> command_;
    std::vector<std::string> env_;
    std::string title_;
};

/// The terminal's xterm exited: end the session, explaining why if it
/// ended too soon.
void terminal_exited(Supervisor& sup, ExitStatus status, std::string_view output);

// The window of a remote desktop (VNC, RDP) is one program that fills
// the screen. The session ends when it exits.

/// Which exit statuses end a desktop session the ordinary way, such as
/// a logout, given the program's output.
using OrdinaryExit = std::function<bool(int code, std::string_view output)>;

/// Starts the desktop program, and with it the session.
void run_desktop(Supervisor& sup, Program program);

/// The desktop program exited: end the session, explaining why unless
/// the exit was an ordinary one.
void desktop_exited(Supervisor& sup, ExitStatus status, std::string_view output,
                    const OrdinaryExit& normal);

/// Reads "WIDTH HEIGHT" from pixterm-xsetup --full-screen, falling back
/// to 1024x768.
[[nodiscard]] std::pair<int, int> screen_size(ExitStatus status, std::string_view output);

} // namespace pixterm::supervisor
