// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "fake_system.hpp"
#include "test.hpp"

#include <pixterm/lines.hpp>
#include <pixterm/pin.hpp>

#include <failure_console.hpp>

#include <csignal>
#include <memory>

using namespace pixterm;
using namespace pixterm::supervisor;
using pixterm::test::FakeSystem;
using pixterm::test::has_sequence;
using pixterm::test::mentions;

namespace {

Host ssh_host() {
    Host h;
    h.id = "desktop";
    h.name = "desktop";
    h.address = "desktop.example.org";
    h.protocol = Protocol::ssh_x11;
    h.port = 22;
    h.username = "alice";
    return h;
}

// A supervisor on a fake system, with helpers for the usual steps.
struct Rig {
    FakeSystem sys;
    std::unique_ptr<Supervisor> sup;

    explicit Rig(std::vector<Host> hosts = {ssh_host()}) {
        sys.settings.hosts = std::move(hosts);
        sup = std::make_unique<Supervisor>(
            sys, Environment{.runtime_dir = "/run/pixterm",
                             .state_dir = "/var/lib/pixterm",
                             .libexec_dir = "/usr/libexec/pixterm",
                             .data_dir = "/usr/share/pixterm",
                             .base_env = {"PATH=/usr/bin:/bin", "DISPLAY=:0"}});
    }

    // Starts, and brings up the chooser.
    void boot() {
        sup->start();
        sup->x_ready();
        sys.finish_job(*sup, ExitStatus::exited(0), "");
    }

    // The chooser and mwm exit when told to.
    void hand_over() {
        sys.exit(*sup, Slot::chooser);
        sys.exit(*sup, Slot::mwm);
    }

    // Everything exits when told to, and the X server restarts.
    void finish_teardown() { sys.reap_signalled(*sup); }

    [[nodiscard]] State state() const { return sup->state(); }
};

} // namespace

TEST(boots_to_the_chooser) {
    Rig r;
    r.sup->start();
    CHECK(r.state() == State::x_start);
    REQUIRE_EQ(r.sys.xorg_starts.size(), 1U);
    CHECK(has_sequence(r.sys.xorg_starts[0], {"-nolisten", "tcp", "-noreset"}));
    CHECK(has_sequence(r.sys.xorg_starts[0], {"-s", "10"})); // blank after 10 minutes
    CHECK(mentions(r.sys.xorg_starts[0], "+iglx"));          // OpenGL from old hosts
    CHECK(!mentions(r.sys.xorg_starts[0], "-dpms"));

    r.sup->x_ready();
    CHECK(r.state() == State::x_setup);
    REQUIRE(r.sys.job_program() != nullptr);
    CHECK_EQ(r.sys.job_program()->argv.at(0), "/usr/libexec/pixterm/pixterm-xsetup");

    r.sys.finish_job(*r.sup, ExitStatus::exited(1), "no such mode");
    CHECK(r.state() == State::local); // a failed xsetup still shows the chooser
    CHECK(r.sys.running(Slot::mwm));
    CHECK(r.sys.running(Slot::chooser));
    CHECK(r.sys.chooser_open);
}

TEST(an_x_server_that_never_gets_ready_is_restarted) {
    Rig r;
    r.sup->start();
    r.sys.advance(*r.sup, timeouts::x_start);
    CHECK(r.state() == State::teardown);
    CHECK(r.sys.was_signalled(Slot::xorg));
    r.finish_teardown();
    CHECK(r.state() == State::x_start);
    r.sys.advance(*r.sup, std::chrono::seconds(2)); // waits after a failure
    CHECK_EQ(r.sys.xorg_starts.size(), 2U);
}

namespace {

constexpr std::string_view failing_log = R"([    1.000] (II) Loading the modesetting driver
[    1.200] (EE) no screens found(EE)
[    1.201] (EE)
[    1.202] (EE) Please also check the log file at "/run/pixterm/Xorg.log" for additional information.
[    1.203] (EE) Server terminated with error (1). Closing log file.
)";

// Xorg exits before it is ready; then the wait before the next try
// passes, unless last.
void fail_x_start(Rig& r, bool last = false) {
    r.sys.exit(*r.sup, Slot::xorg, ExitStatus::exited(1));
    if (!last)
        r.sys.advance(*r.sup, std::chrono::seconds(64));
}

// Three failed starts in a row, which bring up the console.
void fail_x_three_times(Rig& r) {
    r.sys.files["/run/pixterm/Xorg.log"] = failing_log;
    r.sup->start();
    fail_x_start(r);
    fail_x_start(r);
    fail_x_start(r, true);
}

} // namespace

TEST(the_console_explains_an_x_server_that_keeps_failing) {
    Rig r;
    r.sys.files["/run/pixterm/Xorg.log"] = failing_log;
    r.sup->start();
    fail_x_start(r);
    fail_x_start(r);
    CHECK(r.sys.console.empty()); // two failures may be bad luck
    fail_x_start(r, true);
    CHECK(r.sys.console.contains("cannot start its display"));
    CHECK(r.sys.console.contains("no screens found"));
    CHECK(!r.sys.console.contains("Please also check"));
    CHECK(r.sys.console.contains("type reset"));
}

TEST(enter_on_the_console_starts_x_again_at_once) {
    Rig r;
    fail_x_three_times(r);
    const auto starts = r.sys.xorg_starts.size();
    r.sup->console_line("");
    CHECK_EQ(r.sys.xorg_starts.size(), starts + 1);
    r.sup->x_ready();
    CHECK(r.sys.console.empty()); // cleared behind the X screen
    r.sup->console_line("reset"); // no longer offered
    CHECK(!r.sys.helper_request.has_value());
}

TEST(reset_on_the_console_restores_the_factory_settings_and_restarts) {
    Rig r;
    fail_x_three_times(r);
    r.sup->console_line("rest"); // a typo changes nothing
    CHECK(!r.sys.helper_request.has_value());
    r.sup->console_line(" reset ");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args == std::vector<std::string>{"config-reset"});
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = {}});
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args == std::vector<std::string>{"reboot"});
}

TEST(a_failed_reset_is_explained_on_the_console) {
    Rig r;
    fail_x_three_times(r);
    r.sup->console_line("reset");
    r.sys.answer_helper(*r.sup, {.ok = false, .message = "disk full", .payload = {}});
    CHECK(r.sys.console.contains("disk full"));
    CHECK(r.sys.console.contains("type reset")); // offered again
}

TEST(the_console_asks_for_the_administrator_pin_before_a_reset) {
    Rig r;
    r.sys.settings.admin_pin = make_pin_hash("1234").value_or("");
    fail_x_three_times(r);
    r.sup->console_line("reset");
    CHECK(r.sys.console.contains("PIN"));
    CHECK(!r.sys.console_echoes);
    r.sup->console_line("9999");
    CHECK(!r.sys.helper_request.has_value());
    CHECK(r.sys.console_echoes);
    CHECK(r.sys.console.contains("wrong"));

    // Locked for a while after a wrong PIN.
    r.sup->console_line("reset");
    CHECK(r.sys.console_echoes);
    r.sys.clock += std::chrono::seconds(30);
    r.sup->console_line("reset");
    CHECK(!r.sys.console_echoes);
    r.sup->console_line("1234");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args == std::vector<std::string>{"config-reset"});
}

TEST(xorg_errors_leave_out_the_boilerplate) {
    CHECK(xorg_errors(failing_log, 5) == std::vector<std::string>{"no screens found"});
    CHECK(xorg_errors("[ 1.0] (EE) a\n[ 1.1] (EE) b\n[ 1.2] (EE) c\n", 2)
          == (std::vector<std::string>{"b", "c"}));
    CHECK(xorg_errors("", 5).empty());
}

TEST(a_large_screen_gets_large_fonts) {
    Rig r;
    r.sup->start();
    r.sup->x_ready();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "SCREEN 3840 2160\n");
    for (const auto slot : {Slot::chooser, Slot::mwm}) {
        const auto* p = r.sys.program(slot);
        REQUIRE(p != nullptr);
        CHECK(has_sequence(p->env, {"XENVIRONMENT=/usr/share/pixterm/Xresources-large"}));
    }
}

TEST(an_ordinary_screen_gets_ordinary_fonts) {
    Rig r;
    r.sup->start();
    r.sup->x_ready();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "SCREEN 1920 1080\n");
    const auto* p = r.sys.program(Slot::chooser);
    REQUIRE(p != nullptr);
    CHECK(has_sequence(p->env, {"XENVIRONMENT=/usr/share/pixterm/Xresources"}));
}

namespace {

// Logs in to the SSH host; the session then runs on the local display.
void reach_ssh_session(Rig& r) {
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    REQUIRE(r.state() == State::session);
}

} // namespace

TEST(a_session_on_the_local_display_gets_the_console_keys) {
    Rig r;
    reach_ssh_session(r);
    const auto* keys = r.sys.program(Slot::chooser);
    REQUIRE(keys != nullptr);
    CHECK(has_sequence(keys->argv, {"/usr/libexec/pixterm/pixterm-ui", "--session", "desktop"}));
    CHECK(r.sys.chooser_open); // it talks to pixtermd
}

TEST(a_host_that_sends_no_window_manager_gets_mwm) {
    auto host = ssh_host();
    host.local_wm = true;
    Rig r({host});
    reach_ssh_session(r);
    const auto* mwm = r.sys.program(Slot::mwm);
    REQUIRE(mwm != nullptr);
    CHECK_EQ(mwm->name, "mwm");
}

TEST(mwm_for_an_8_bit_host_runs_on_the_8_bit_display) {
    auto host = ssh_host();
    host.local_wm = true;
    host.pseudocolor = true;
    Rig r({host});
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "1024 768");
    r.sys.sockets.insert("/tmp/.X11-unix/X1");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    REQUIRE(r.state() == State::session);
    const auto* mwm = r.sys.program(Slot::mwm);
    REQUIRE(mwm != nullptr);
    CHECK(mentions(mwm->env, "DISPLAY=:1"));
    CHECK(!mentions(mwm->env, "DISPLAY=:0"));
}

TEST(a_programs_own_variable_replaces_the_terminals) {
    Rig r;
    const auto env = r.sup->child_env({"DISPLAY=:1"});
    CHECK(mentions(env, "DISPLAY=:1"));
    CHECK(!mentions(env, "DISPLAY=:0"));
    CHECK(mentions(env, "PATH=/usr/bin:/bin"));
}

TEST(a_session_has_no_mwm_unless_asked) {
    Rig r;
    reach_ssh_session(r);
    CHECK(r.sys.program(Slot::mwm) == nullptr);
}

TEST(disconnect_from_the_console_ends_the_session) {
    Rig r;
    reach_ssh_session(r);
    r.sup->chooser_line("DISCONNECT");
    CHECK(r.state() == State::teardown);
    CHECK(mentions(r.sys.logs, "disconnected from the console"));
}

TEST(disconnect_means_nothing_at_the_chooser) {
    Rig r;
    r.boot();
    r.sup->chooser_line("DISCONNECT");
    CHECK(r.state() == State::local);
}

TEST(a_crashed_chooser_comes_back) {
    Rig r;
    r.boot();
    r.sys.clock += std::chrono::seconds(60);
    r.sys.exit(*r.sup, Slot::chooser, ExitStatus::killed(SIGSEGV));
    CHECK(!r.sys.running(Slot::chooser));
    r.sys.advance(*r.sup, timeouts::restart_delay);
    CHECK(r.sys.running(Slot::chooser));
    CHECK(r.state() == State::local);
}

TEST(a_chooser_that_keeps_crashing_resets_the_display) {
    Rig r;
    r.boot();
    for (int i = 0; i < quick_failure_limit; ++i) {
        r.sys.exit(*r.sup, Slot::chooser, ExitStatus::exited(1));
        if (i + 1 < quick_failure_limit)
            r.sys.advance(*r.sup, timeouts::restart_delay);
    }
    CHECK(r.state() == State::teardown);
}

TEST(an_x_server_crash_is_explained_when_the_chooser_returns) {
    Rig r;
    r.boot();
    r.sys.exit(*r.sup, Slot::xorg, ExitStatus::killed(SIGSEGV));
    CHECK(r.state() == State::teardown);
    r.finish_teardown();
    r.sys.advance(*r.sup, std::chrono::milliseconds(0));
    REQUIRE_EQ(r.sys.xorg_starts.size(), 2U);
    r.sup->x_ready();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "");
    CHECK(r.sys.was_sent("ERROR The display server stopped unexpectedly."));
}

TEST(an_unknown_host_is_refused) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START nosuch -");
    CHECK(r.sys.was_sent("ERROR "));
    CHECK(r.sys.was_sent("IDLE"));
    CHECK(r.state() == State::local);
}

TEST(requests_in_the_wrong_state_are_ignored) {
    Rig r;
    r.sup->start();
    r.sup->chooser_line("START desktop -");
    CHECK(r.state() == State::x_start);
    CHECK(!r.sys.running(Slot::master));
}

TEST(ssh_logs_in_while_the_chooser_is_up) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    CHECK(r.state() == State::connecting);
    const auto* master = r.sys.program(Slot::master);
    REQUIRE(master != nullptr);
    CHECK(has_sequence(master->argv, {"-o", "ControlMaster=yes"}));
    CHECK(has_sequence(master->argv, {"-l", "alice", "--", "desktop.example.org"}));
    CHECK(mentions(master->env, "SSH_ASKPASS=/usr/libexec/pixterm/pixterm-askpass"));

    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    CHECK(r.state() == State::connecting); // no control socket yet

    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    CHECK(r.state() == State::handoff);
    CHECK(r.sys.was_sent("HANDOFF"));
    CHECK(!r.sys.chooser_open);
    CHECK(r.sys.was_signalled(Slot::mwm));

    r.hand_over();
    CHECK(r.state() == State::session);
    const auto* client = r.sys.program(Slot::client);
    REQUIRE(client != nullptr);
    CHECK(has_sequence(client->argv, {"-o", "ControlMaster=no"}));
    CHECK(client->hold_input);
    CHECK(client->argv.back().contains(".xsession"));
}

TEST(ssh_logout_returns_to_the_chooser) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.clock += std::chrono::minutes(30);
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(0));
    CHECK(r.state() == State::teardown);
    r.finish_teardown();
    r.sys.advance(*r.sup, std::chrono::milliseconds(0));
    CHECK_EQ(r.sys.xorg_starts.size(), 2U);
    CHECK(r.sys.logs.end() != std::ranges::find_if(r.sys.logs, [](const std::string& l) {
              return l.contains("session on desktop ended");
          }));
}

TEST(a_failed_ssh_login_is_shown_in_the_chooser) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.exit(*r.sup, Slot::master, ExitStatus::exited(255), "Permission denied (password).");
    CHECK(r.state() == State::local);
    CHECK(r.sys.was_sent("ERROR Could not log in to desktop.\\n\\nPermission denied"));
    CHECK(r.sys.was_sent("IDLE"));
}

TEST(a_cancelled_ssh_login_ends_quietly) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sup->chooser_line("CANCEL");
    CHECK(r.sys.was_signalled(Slot::master));
    r.sys.reap_signalled(*r.sup);
    CHECK(r.state() == State::local);
    CHECK(!r.sys.was_sent("ERROR"));
    CHECK(r.sys.was_sent("IDLE"));
}

TEST(a_session_that_ends_at_once_is_an_error) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(0));
    CHECK(r.state() == State::teardown);
    r.finish_teardown();
    r.sys.advance(*r.sup, std::chrono::milliseconds(0));
    r.sup->x_ready();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "");
    CHECK(r.sys.was_sent("ERROR The session on desktop ended immediately"));
}

TEST(a_display_reset_is_reported_instead_of_the_session_error) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.clock += std::chrono::minutes(5);
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(1), "X connection broken");
    CHECK(r.state() == State::session); // waits to see whether Xorg goes too
    r.sys.exit(*r.sup, Slot::xorg, ExitStatus::exited(0));
    CHECK(r.state() == State::teardown);
    r.finish_teardown();
    r.sys.advance(*r.sup, std::chrono::milliseconds(0));
    r.sup->x_ready();
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "");
    CHECK(r.sys.was_sent("ERROR The session on desktop was ended by a display reset."));
    CHECK(!r.sys.was_sent("ERROR The connection"));
}

TEST(a_session_error_is_reported_after_the_grace_period) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.clock += std::chrono::minutes(5);
    r.sys.exit(*r.sup, Slot::client, ExitStatus::exited(255), "broken pipe");
    r.sys.advance(*r.sup, timeouts::display_reset_grace);
    CHECK(r.state() == State::teardown);
}

TEST(losing_the_network_ends_the_session) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.hand_over();
    r.sys.carrier = false;
    for (int i = 0; i < 10; ++i) {
        r.sys.advance(*r.sup, timeouts::watch_interval);
        CHECK(r.state() == State::session);
    }
    r.sys.advance(*r.sup, timeouts::watch_interval);
    CHECK(r.state() == State::teardown);
}

TEST(a_handoff_that_hangs_is_forced) {
    Rig r;
    r.boot();
    r.sup->chooser_line("START desktop -");
    r.sys.sockets.insert("/run/pixterm/ssh-control");
    r.sys.advance(*r.sup, std::chrono::milliseconds(200));
    r.sys.signals.clear();
    r.sys.advance(*r.sup, timeouts::handoff);
    CHECK(std::ranges::count(r.sys.signals, std::pair{Slot::mwm, SIGKILL}) == 1);
}

TEST(applying_settings_saves_them_through_the_helper) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] =
        R"({"schema_version":1,"device":{"keyboard":{"layout":"de"}}})";
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK_EQ(r.sys.helper_request->args.at(0), "config-save");
    CHECK(r.sys.helper_request->payload.contains("\"de\""));
    CHECK(!r.sys.files.contains("/run/pixterm/config.candidate.json"));

    r.sys.settings.keyboard_layout = "de"; // what the helper saved
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = {}});
    // The keyboard changed, so pixterm-xsetup applies it.
    REQUIRE(r.sys.job_program() != nullptr);
    r.sys.finish_job(*r.sup, ExitStatus::exited(0), "");
    CHECK(r.sys.was_sent("APPLIED"));
}

TEST(xorg_never_blanks_the_screen_when_told_not_to) {
    Rig r;
    r.sys.settings.blank_minutes = 0;
    r.sup->start();
    REQUIRE_EQ(r.sys.xorg_starts.size(), 1U);
    CHECK(has_sequence(r.sys.xorg_starts[0], {"-s", "0", "-dpms"}));
}

TEST(a_new_blanking_time_is_applied_at_once) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] =
        R"({"schema_version":1,"device":{"display":{"blank_minutes":30}}})";
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    r.sys.settings.blank_minutes = 30; // what the helper saved
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = {}});
    REQUIRE(r.sys.job_program() != nullptr);
    CHECK_EQ(r.sys.job_program()->name, "pixterm-xsetup");
}

namespace {

constexpr std::string_view join_home =
    R"({"schema_version":1,"device":{"wifi":{"enabled":true,"ssid":"home","country":"GB"}}})";

} // namespace

TEST(joining_wifi_hands_the_passphrase_to_the_helper_before_saving) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] = join_home;
    r.sys.files["/run/pixterm/wifi.passphrase"] = "correct horse";
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args
          == (std::vector<std::string>{"wifi", "on", "GB", "686f6d65", "0"}));
    CHECK_EQ(r.sys.helper_request->payload, "correct horse");
    CHECK(!r.sys.files.contains("/run/pixterm/wifi.passphrase"));
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = {}});
    REQUIRE(r.sys.helper_request.has_value());
    CHECK_EQ(r.sys.helper_request->args.at(0), "config-save");
    CHECK(!r.sys.helper_request->payload.contains("correct horse"));
}

TEST(unchanged_wifi_is_saved_without_the_helper_joining_again) {
    Rig r;
    r.sys.settings = parse_settings(join_home).value_or(Settings{});
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] = join_home;
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK_EQ(r.sys.helper_request->args.at(0), "config-save");
}

TEST(turning_wifi_off_tells_the_helper) {
    Rig r;
    r.sys.settings = parse_settings(join_home).value_or(Settings{});
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] =
        R"({"schema_version":1,"device":{"wifi":{"enabled":false,"ssid":"home","country":"GB"}}})";
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args == (std::vector<std::string>{"wifi", "off"}));
}

TEST(wifi_that_cannot_be_joined_is_not_saved) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] = join_home;
    r.sup->chooser_line("APPLY");
    r.sys.answer_helper(*r.sup,
                        {.ok = false, .message = "Type the passphrase for home.", .payload = {}});
    CHECK(!r.sys.helper_request.has_value());
    CHECK(r.sys.was_sent("APPLY_FAILED"));
}

TEST(a_bad_passphrase_never_reaches_the_helper) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] = join_home;
    r.sys.files["/run/pixterm/wifi.passphrase"] = "short";
    r.sup->chooser_line("APPLY");
    CHECK(!r.sys.helper_request.has_value());
    CHECK(r.sys.was_sent("APPLY_FAILED"));
    CHECK(!r.sys.files.contains("/run/pixterm/wifi.passphrase"));
}

TEST(a_wifi_scan_answers_with_what_the_helper_found) {
    Rig r;
    r.boot();
    r.sup->chooser_line("WIFI_SCAN GB");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args == (std::vector<std::string>{"wifi-scan", "GB"}));
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = "-48 wpa2 686f6d65\n"});
    CHECK(r.sys.was_sent("APPLIED " + escape_line("-48 wpa2 686f6d65\n")));
    r.sup->chooser_line("WIFI_SCAN Britain");
    CHECK(!r.sys.helper_request.has_value());
    CHECK(r.sys.was_sent("APPLY_FAILED"));
}

TEST(forgetting_a_host_key_names_the_host_to_the_helper) {
    Rig r;
    r.boot();
    r.sup->chooser_line("FORGET_KEY " + ssh_host().id);
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args
          == (std::vector<std::string>{"host-key-forget", ssh_host().address,
                                       std::to_string(ssh_host().port)}));
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = {}});
    CHECK(r.sys.was_sent("APPLIED"));
    r.sup->chooser_line("FORGET_KEY nobody");
    CHECK(!r.sys.helper_request.has_value());
    CHECK(r.sys.was_sent("APPLY_FAILED"));
}

TEST(diagnostics_are_saved_through_the_helper) {
    Rig r;
    r.boot();
    r.sup->chooser_line("DIAGNOSTICS");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args == std::vector<std::string>{"usb-diagnostics"});
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = "Saved."});
    CHECK(r.sys.was_sent("APPLIED Saved."));
}

TEST(a_new_time_zone_is_applied_before_saving) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] =
        R"({"schema_version":1,"device":{"time":{"zone":"Europe/Paris","ntp":["ntp.lab"]}}})";
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args
          == (std::vector<std::string>{"time", "Europe/Paris", "ntp.lab", "-"}));
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = {}});
    REQUIRE(r.sys.helper_request.has_value());
    CHECK_EQ(r.sys.helper_request->args.at(0), "config-save");
}

TEST(new_network_settings_are_applied_before_saving) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] =
        R"({"schema_version":1,"device":{"network":{"method":"static","address":"10.0.0.2/24"}}})";
    r.sup->chooser_line("APPLY");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK(r.sys.helper_request->args
          == (std::vector<std::string>{"network", "static", "10.0.0.2/24", "-", "-", "-"}));
    r.sys.answer_helper(*r.sup, {.ok = false, .message = "no carrier", .payload = {}});
    CHECK(r.sys.was_sent("APPLY_FAILED The network settings could not be applied."));
    CHECK(!r.sys.helper_request.has_value()); // nothing saved
}

TEST(invalid_new_settings_are_refused) {
    Rig r;
    r.boot();
    r.sys.files["/run/pixterm/config.candidate.json"] = R"({"schema_version":9})";
    r.sup->chooser_line("APPLY");
    CHECK(r.sys.was_sent("APPLY_FAILED The new settings are not valid."));
    CHECK(!r.sys.helper_request.has_value());
}

TEST(one_change_at_a_time) {
    Rig r;
    r.boot();
    r.sup->chooser_line("EXPORT");
    REQUIRE(r.sys.helper_request.has_value());
    CHECK_EQ(r.sys.helper_request->args.at(0), "usb-export");
    r.sup->chooser_line("RESET");
    CHECK(r.sys.was_sent("APPLY_FAILED Another change is still in progress."));
    r.sup->chooser_line("START desktop -");
    CHECK(!r.sys.running(Slot::master));
    r.sys.answer_helper(*r.sup, {.ok = true, .message = {}, .payload = "Saved to sda1."});
    CHECK(r.sys.was_sent("APPLIED Saved to sda1."));
}
