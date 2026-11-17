// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// XDMCP hosts. The probe first asks the host with an XDMCP query
// whether it is willing, so an unreachable host is reported while the
// chooser is up. After the handoff, the local Xorg is replaced by one
// that runs -query (or -indirect) host -once. The host's display
// manager logs the user in; -once makes Xorg exit when the session
// ends, which is the logout signal (state remote).

#include "parts.hpp"
#include "sessions.hpp"

#include <format>

namespace pixterm::supervisor {

namespace {

constexpr Duration probe_timeout{12'000}; // a name lookup, then three queries
constexpr Duration refused_within{15'000};
constexpr Duration greeter_within{30'000};
constexpr int x_tcp_port = 6000;

// Xorg's built-in font path on Debian; Xorg skips missing directories.
constexpr std::string_view default_font_path =
    "/usr/share/fonts/X11/misc,/usr/share/fonts/X11/cyrillic,"
    "/usr/share/fonts/X11/100dpi/:unscaled,/usr/share/fonts/X11/75dpi/:unscaled,"
    "/usr/share/fonts/X11/Type1,/usr/share/fonts/X11/100dpi,"
    "/usr/share/fonts/X11/75dpi,built-ins";

class XdmcpSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view username) override;
    void cancel() override;
    void start() override { sup().switch_display(); }
    [[nodiscard]] std::vector<std::string> xorg_args() const override;
    void display_ready() override;
    void watch(Time now) override;
    void xorg_exited(ExitStatus status) override;

private:
    Time began_;
    bool greeter_seen_ = false;

    [[nodiscard]] const Host& host() const { return sup().host(); }
    void probe_done(std::string_view output);
};

bool XdmcpSession::begin(std::string_view /*username*/) {
    // The display manager asks for the user name itself.
    sup().log(std::format("asking {} ({}) for XDMCP", host().name, host().address));
    const bool started = sup().system().start_job(
        {.name = "xdmcp-probe",
         .task = XdmcpProbe{.address = host().address,
                            .port = host().port,
                            .indirect = host().xdmcp_mode == XdmcpMode::indirect},
         .timeout = probe_timeout,
         .secret = false},
        [this](ExitStatus, std::string_view output) { probe_done(output); });
    if (!started) {
        sup().report_error("Something else is still in progress; try again.");
        return false;
    }
    sup().set_state(State::probe);
    return true;
}

void XdmcpSession::probe_done(std::string_view output) {
    if (sup().state() != State::probe)
        return;
    sup().set_state(State::local);
    const auto rest = output.size() > 2 ? output.substr(2) : std::string_view();
    if (output.starts_with('W')) {
        sup().log(std::format("{} is willing: {}", host().name, rest));
        sup().start_handoff();
        return;
    }
    if (output.starts_with('U')) {
        const auto tab = rest.find('\t');
        sup().report_error(
            std::format("{} declined the XDMCP connection.{}", host().name,
                        paragraph(tab == std::string_view::npos ? "" : rest.substr(tab + 1))));
    } else if (output.starts_with('R')) {
        sup().report_error(std::format("The address of {} ({}) could not be found:\n{}",
                                       host().name, host().address, rest));
    } else {
        sup().report_error(std::format(
            "{} ({}) did not answer the XDMCP query.\n\n"
            "Check that the host runs a display manager with XDMCP turned on, and that its "
            "access list (Xaccess) lets this terminal in.",
            host().name, host().address));
    }
    sup().chooser_idle();
}

void XdmcpSession::cancel() {
    sup().log(std::format("XDMCP query to {} cancelled", host().name));
    sup().system().cancel_job();
    sup().set_state(State::local);
    sup().chooser_idle();
}

std::vector<std::string> XdmcpSession::xorg_args() const {
    // The display manager's programs connect back over TCP, unencrypted.
    // No -noreset: the reset at the end of the session is what makes
    // -once stop Xorg.
    std::vector<std::string> a{"-listen", "tcp",
                               host().xdmcp_mode == XdmcpMode::indirect ? "-indirect" : "-query",
                               host().address, "-once"};
    if (host().port != default_port(Protocol::xdmcp))
        a.insert(a.end(), {"-port", std::to_string(host().port)});
    // On the command line, the font path survives server resets.
    if (!host().font_server.empty())
        a.insert(a.end(), {"-fp", std::format("{},{}", default_font_path, host().font_server)});
    return a;
}

void XdmcpSession::display_ready() {
    sup().log(std::format("XDMCP {} to {} ({})",
                          host().xdmcp_mode == XdmcpMode::indirect ? "indirect query" : "query",
                          host().name, host().address));
    began_ = sup().system().now();
    greeter_seen_ = false;
    sup().session_running(State::remote);
}

// The host must connect back to the display, and show its login, in
// time.
void XdmcpSession::watch(Time now) {
    if (sup().state() != State::remote || greeter_seen_)
        return;
    const auto clients = sup().system().display_clients(false);
    if (clients != 0) {
        greeter_seen_ = true; // connected, or we cannot tell
    } else if (now - began_ >= greeter_within) {
        sup().report_error(std::format(
            "{} accepted the XDMCP connection but never showed a login.\n\n"
            "The host must be able to reach this terminal on TCP port {}; check the firewalls "
            "between them.",
            host().name, x_tcp_port));
        sup().teardown();
    }
}

void XdmcpSession::xorg_exited(ExitStatus /*status*/) {
    // A login that connected means a real session, however short.
    if (!greeter_seen_ && sup().system().now() - sup().started(Slot::xorg) < refused_within)
        sup().report_error(std::format(
            "The XDMCP session with {} ended immediately.\n\n"
            "The host answered but refused the session; check its display manager's log.",
            host().name));
    else
        sup().log(std::format("XDMCP session on {} ended", host().name));
}

} // namespace

std::unique_ptr<Session> make_xdmcp_session(Supervisor& sup) {
    return std::make_unique<XdmcpSession>(sup);
}

} // namespace pixterm::supervisor
