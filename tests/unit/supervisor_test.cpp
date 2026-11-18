// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "fake_system.hpp"
#include "test.hpp"

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
