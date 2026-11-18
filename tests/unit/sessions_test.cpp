// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "fake_system.hpp"
#include "test.hpp"

#include <sessions/parts.hpp>

#include <csignal>
#include <memory>

using namespace pixterm;
using namespace pixterm::supervisor;
using pixterm::test::FakeSystem;
using pixterm::test::has_sequence;
using pixterm::test::mentions;

namespace {

Host make_host(Protocol p, std::string address = "host.lan") {
    Host h;
    h.id = "h";
    h.name = "Host";
    h.address = std::move(address);
    h.protocol = p;
    h.port = default_port(p);
    return h;
}

struct Rig {
    FakeSystem sys;
    std::unique_ptr<Supervisor> sup;

    explicit Rig(const Host& host) {
        sys.settings.hosts = {host};
        sup = std::make_unique<Supervisor>(sys, Environment{.runtime_dir = "/run/pixterm",
                                                            .state_dir = "/var/lib/pixterm",
                                                            .libexec_dir = "/usr/libexec/pixterm",
                                                            .data_dir = "/usr/share/pixterm",
                                                            .base_env = {"PATH=/usr/bin:/bin"}});
        sup->start();
        sup->x_ready();
        sys.finish_job(*sup, ExitStatus::exited(0), "");
    }

    void pick(std::string_view username = "-") const {
        sup->chooser_line(std::string("START h ") + std::string(username));
    }

    void hand_over() {
        sys.exit(*sup, Slot::chooser);
        sys.exit(*sup, Slot::mwm);
    }

    // The local X server exits and the session's own one comes up.
    void switch_display() {
        CHECK(sup->state() == State::x_switch);
        sys.exit(*sup, Slot::xorg, ExitStatus::killed(SIGTERM));
        sup->x_ready();
    }

    [[nodiscard]] State state() const { return sup->state(); }
};

} // namespace

TEST(xdmcp_asks_the_host_then_gives_it_the_display) {
    auto host = make_host(Protocol::xdmcp, "xdmhost");
    host.font_server = "tcp/xdmhost:7100";
    Rig r(host);
    r.pick();
    CHECK(r.state() == State::probe);
    REQUIRE(r.sys.job.has_value());
    const auto* probe = std::get_if<XdmcpProbe>(&r.sys.job->task);
    REQUIRE(probe != nullptr);
    CHECK_EQ(probe->address, "xdmhost");

    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "W xdmhost\tload 0.5");
    CHECK(r.state() == State::handoff);
    r.hand_over();
    r.switch_display();
    REQUIRE_EQ(r.sys.xorg_starts.size(), 2U);
    const auto& args = r.sys.xorg_starts[1];
    CHECK(has_sequence(args, {"-query", "xdmhost", "-once"}));
    CHECK(mentions(args, ",tcp/xdmhost:7100"));
    CHECK(!mentions(args, "-noreset"));
    CHECK(r.state() == State::remote); // no pixterm-xsetup on its display
    CHECK(!r.sys.job.has_value());
}

TEST(xdmcp_explains_a_host_that_does_not_answer) {
    Rig r(make_host(Protocol::xdmcp));
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "N");
    CHECK(r.state() == State::local);
    CHECK(r.sys.was_sent("ERROR Host (host.lan) did not answer the XDMCP query."));
    CHECK(r.sys.was_sent("IDLE"));
}

TEST(xdmcp_reports_a_login_that_never_appears) {
    Rig r(make_host(Protocol::xdmcp));
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "W host\t");
    r.hand_over();
    r.switch_display();
    for (int i = 0; i < 29; ++i)
        r.sys.advance(*r.sup, timeouts::watch_interval);
    CHECK(r.state() == State::remote);
    r.sys.advance(*r.sup, timeouts::watch_interval);
    r.sys.advance(*r.sup, timeouts::watch_interval);
    CHECK(r.state() == State::teardown);
}

TEST(xdmcp_logout_ends_the_session) {
    Rig r(make_host(Protocol::xdmcp));
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "W host\t");
    r.hand_over();
    r.switch_display();
    r.sys.clients = 3;
    r.sys.advance(*r.sup, timeouts::watch_interval);
    r.sys.clock += std::chrono::minutes(20);
    r.sys.exit(*r.sup, Slot::xorg, ExitStatus::exited(0));
    // Nothing else was running, so the local display restarts at once.
    CHECK(r.state() == State::x_start);
    CHECK(r.sys.logs.end() != std::ranges::find(r.sys.logs, "XDMCP session on Host ended"));
    CHECK(!mentions(r.sys.logs, "error: "));
}

TEST(telnet_checks_the_host_then_opens_a_terminal) {
    Rig r(make_host(Protocol::telnet, "vax"));
    r.pick();
    REQUIRE(r.sys.job.has_value());
    CHECK(std::holds_alternative<TcpProbe>(r.sys.job->task));
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.hand_over();
    REQUIRE(r.sys.job_program() != nullptr); // pixterm-xsetup --full-screen
    CHECK(mentions(r.sys.job_program()->argv, "--full-screen"));
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "1920 1080");
    CHECK(r.state() == State::session);
    const auto* xterm = r.sys.program(Slot::client);
    REQUIRE(xterm != nullptr);
    CHECK(has_sequence(xterm->argv, {"-geometry", "191x53+0+0", "-fn", "10x20"}));
    CHECK(has_sequence(xterm->argv, {"-e", "/usr/bin/telnet", "-8", "-E", "vax", "23"}));
}

TEST(telnet_reports_an_unreachable_host) {
    Rig r(make_host(Protocol::telnet, "vax"));
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "E The host did not answer.");
    CHECK(r.state() == State::local);
    CHECK(r.sys.was_sent("ERROR Could not connect to Host (vax, port 23).\\n\\nThe host did not"));
}

TEST(terminal_hosts_may_let_the_host_open_windows) {
    auto host = make_host(Protocol::telnet, "vms");
    host.allow_x = true;
    Rig r(host);
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.hand_over();
    r.switch_display();
    CHECK(has_sequence(r.sys.xorg_starts[1], {"-listen", "tcp"}));
    REQUIRE(r.sys.job_program() != nullptr);
    CHECK(has_sequence(r.sys.job_program()->argv, {"--allow-host", "vms"}));
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "DISPLAY 10.0.0.5:0\n");
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "1024 768"); // --full-screen
    const auto* xterm = r.sys.program(Slot::client);
    REQUIRE(xterm != nullptr);
    CHECK(mentions(xterm->argv, "Host - X display 10.0.0.5:0"));
}

TEST(direct_x_waits_for_windows_and_ends_after_the_last) {
    Rig r(make_host(Protocol::direct_x, "lispm"));
    r.pick();
    CHECK(r.state() == State::handoff); // nothing to check first
    r.hand_over();
    r.switch_display();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "DISPLAY 10.0.0.5:0");
    CHECK(r.state() == State::remote);
    const auto* notice = r.sys.program(Slot::chooser);
    REQUIRE(notice != nullptr);
    CHECK(mentions(notice->argv, "use the display 10.0.0.5:0"));

    r.sys.tcp_clients = 1;
    r.sys.advance(*r.sup, timeouts::watch_interval);
    CHECK(r.sys.was_signalled(Slot::chooser));
    r.sys.tcp_clients = 0;
    for (int i = 0; i < 5; ++i)
        r.sys.advance(*r.sup, timeouts::watch_interval);
    CHECK(r.state() == State::teardown);
}

TEST(rexec_asks_for_a_password_and_passes_it_on_standard_input) {
    auto host = make_host(Protocol::rexec, "indy");
    host.username = "guest";
    host.command = "xterm -display %D -ls";
    Rig r(host);
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    REQUIRE(r.sys.job.has_value());
    CHECK(r.sys.job->secret);
    CHECK(mentions(r.sys.job_program()->argv, "Password for guest on Host:"));
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "hunter2\n");
    CHECK(r.state() == State::handoff);
    r.hand_over();
    r.switch_display();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "DISPLAY 10.0.0.5:0");
    const auto* rexec = r.sys.program(Slot::client);
    REQUIRE(rexec != nullptr);
    CHECK(has_sequence(rexec->argv,
                       {"rexec", "indy", "512", "guest", "xterm -display 10.0.0.5:0 -ls"}));
    CHECK_EQ(rexec->input, "hunter2\n");
    CHECK(!mentions(rexec->argv, "hunter2"));
    CHECK(r.state() == State::session);
}

TEST(rsh_needs_no_password) {
    auto host = make_host(Protocol::rsh, "sparc");
    host.username = "chris";
    host.command = "xclock -display %D";
    Rig r(host);
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    CHECK(r.state() == State::handoff);
}

TEST(rexec_explains_a_refused_command) {
    auto host = make_host(Protocol::rexec, "indy");
    host.username = "guest";
    host.command = "xterm";
    Rig r(host);
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "pw");
    r.hand_over();
    r.switch_display();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "DISPLAY 10.0.0.5:0");
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(2), "Login incorrect.");
    CHECK(r.state() == State::teardown);
}

TEST(rdp_checks_the_password_before_handing_over) {
    auto host = make_host(Protocol::rdp, "nt");
    host.username = "chris";
    host.domain = "LAB";
    Rig r(host);
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "secret");
    REQUIRE(r.sys.job.has_value());
    CHECK(r.sys.job->secret);
    const auto* check = r.sys.job_program();
    REQUIRE(check != nullptr);
    CHECK(mentions(check->argv, "+auth-only"));
    CHECK(mentions(check->argv, "/d:LAB"));
    CHECK_EQ(check->input, "secret\n");

    r.sys.finish_job(*r.sup, ExitStatus::exited(132),
                     "[ERROR][x]: ERRCONNECT_AUTHENTICATION_FAILED");
    CHECK(r.state() == State::local);
    CHECK(
        r.sys.was_sent("ERROR Could not log in to Host.\\n\\nThe user name or password is wrong."));
}

TEST(rdp_runs_full_screen_with_the_password_on_standard_input) {
    auto host = make_host(Protocol::rdp, "nt");
    host.username = "chris";
    Rig r(host);
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "secret");
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "");
    r.hand_over();
    const auto* rdp = r.sys.program(Slot::client);
    REQUIRE(rdp != nullptr);
    CHECK(mentions(rdp->argv, "/f"));
    CHECK_EQ(rdp->input, "secret\n");
    r.sys.clock += std::chrono::minutes(10);
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(145)); // told to stop
    CHECK(r.state() == State::teardown);
    CHECK(r.sys.logs.end() != std::ranges::find(r.sys.logs, "session on Host ended"));
}

TEST(vnc_fills_the_screen) {
    Rig r(make_host(Protocol::vnc, "imac"));
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.hand_over();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "2560 1440");
    const auto* viewer = r.sys.program(Slot::client);
    REQUIRE(viewer != nullptr);
    CHECK(has_sequence(viewer->argv, {"-geometry", "2560x1440+0+0"}));
    CHECK_EQ(viewer->argv.back(), "imac::5900");
}

TEST(vnc_treats_being_told_to_stop_as_ordinary) {
    Rig r(make_host(Protocol::vnc, "imac"));
    r.pick();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "OK");
    r.hand_over();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "1024 768");
    r.sys.clock += std::chrono::minutes(10);
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(1), "Termination signal received");
    CHECK(r.state() == State::teardown);
    CHECK(!mentions(r.sys.logs, "error: "));
}

TEST(serial_checks_the_port_before_closing_the_chooser) {
    auto host = make_host(Protocol::serial, "");
    host.device = "/dev/ttyUSB0";
    Rig r(host);
    r.pick();
    CHECK(r.state() == State::local);
    CHECK(r.sys.was_sent("ERROR There is no serial port /dev/ttyUSB0."));

    r.sys.devices["/dev/ttyUSB0"] = {};
    r.pick();
    CHECK(r.state() == State::handoff);
    r.hand_over();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "800 600");
    const auto* xterm = r.sys.program(Slot::client);
    REQUIRE(xterm != nullptr);
    CHECK(has_sequence(xterm->argv, {"/usr/bin/picocom", "--baud", "9600"}));
    CHECK(has_sequence(xterm->argv, {"--send-cmd", "", "--receive-cmd", ""}));
    CHECK(has_sequence(xterm->argv, {"-fn", "6x13"}));
}

TEST(ssh_terminals_run_in_an_xterm_without_forwarding) {
    auto host = make_host(Protocol::ssh_terminal, "vms");
    host.username = "ALICE";
    host.auth = Auth::key;
    Rig r(host);
    r.sys.files["/var/lib/pixterm/id_ed25519"] = "key";
    r.pick();
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "1024 768");
    const auto* xterm = r.sys.program(Slot::client);
    REQUIRE(xterm != nullptr);
    CHECK(has_sequence(xterm->argv, {"-o", "ForwardX11=no"}));
    CHECK(has_sequence(xterm->argv, {"-tt", "-e", "none"}));
    CHECK(has_sequence(xterm->argv, {"-o", "IdentitiesOnly=yes"}));
}

TEST(ssh_key_login_needs_the_terminal_key) {
    auto host = make_host(Protocol::ssh_x11);
    host.username = "x";
    host.auth = Auth::key;
    const Rig r(host);
    r.pick();
    CHECK(r.state() == State::local);
    CHECK(r.sys.was_sent("ERROR Host is set to log in with this terminal's key"));
}

TEST(ssh_asks_for_a_user_name_it_does_not_have) {
    const Rig r(make_host(Protocol::ssh_x11));
    r.pick("-"); // the host has no user name
    CHECK(r.state() == State::local);
    CHECK(r.sys.was_sent("ERROR That user name is not valid."));
    r.pick("alice");
    CHECK(r.state() == State::connecting);
}

TEST(ssh_adds_the_font_server_before_the_session) {
    auto host = make_host(Protocol::ssh_x11);
    host.username = "x";
    host.font_server = "tcp/fs:7100";
    Rig r(host);
    r.pick();
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    REQUIRE(r.sys.job_program() != nullptr);
    CHECK(has_sequence(r.sys.job_program()->argv, {"--font-server", "tcp/fs:7100"}));
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "");
    CHECK(r.state() == State::session);
}

TEST(ssh_saves_new_host_keys) {
    auto host = make_host(Protocol::ssh_x11);
    host.username = "x";
    Rig r(host);
    r.pick();
    r.sys.files["/run/pixterm/known_hosts"] = "host.lan ssh-ed25519 AAAA\n";
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    REQUIRE(r.sys.helper_request.has_value());
    CHECK_EQ(r.sys.helper_request->args.at(0), "known-hosts-save");
    CHECK_EQ(r.sys.helper_request->payload, "host.lan ssh-ed25519 AAAA\n");
}

TEST(terminal_font_follows_the_screen_height) {
    const auto [geometry, font] = TerminalWindow::layout(1920, 1080);
    CHECK_EQ(geometry, "191x53+0+0");
    CHECK_EQ(font, "10x20");
    CHECK(TerminalWindow::layout(1024, 768).second == "6x13");
    CHECK(TerminalWindow::layout(1280, 1024).second == "9x15");
    CHECK(TerminalWindow::layout(2560, 1440).second == "10x20");
    CHECK(TerminalWindow::layout(3840, 2160).second == "12x24");
    const std::pair fallback{1024, 768};
    const std::pair big{3840, 2160};
    CHECK(screen_size(ExitStatus::exited(1), "") == fallback);
    CHECK(screen_size(ExitStatus::exited(0), "10 10") == fallback);
    CHECK(screen_size(ExitStatus::exited(0), "3840 2160\n") == big);
}
