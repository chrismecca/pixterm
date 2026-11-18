// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <host_form.hpp>

using namespace pixterm;
using namespace pixterm::chooser;

namespace {

HostForm ssh_form() {
    HostForm f;
    f.name = "Desktop";
    f.address = "desktop.example.org";
    f.port = "22";
    return f;
}

} // namespace

TEST(fields_follow_the_protocol) {
    const auto ssh = enabled_fields(Protocol::ssh_x11, SessionType::xsession);
    CHECK(ssh.address && ssh.port && ssh.username && ssh.key_auth && ssh.session);
    CHECK(ssh.font_server);
    CHECK(!ssh.command);
    CHECK(!ssh.device);
    CHECK(!ssh.speed);
    CHECK(!ssh.domain);
    CHECK(!ssh.allow_x);
    CHECK(!ssh.indirect);

    const auto serial = enabled_fields(Protocol::serial, SessionType::xsession);
    CHECK(serial.device && serial.speed);
    CHECK(!serial.address);
    CHECK(!serial.port);
    CHECK(!serial.username);
    CHECK(!serial.font_server);

    const auto direct = enabled_fields(Protocol::direct_x, SessionType::xsession);
    CHECK(direct.address);
    CHECK(!direct.port);
    CHECK(!direct.font_server);

    CHECK(enabled_fields(Protocol::xdmcp, SessionType::xsession).indirect);
    CHECK(enabled_fields(Protocol::rdp, SessionType::xsession).domain);
    CHECK(enabled_fields(Protocol::telnet, SessionType::xsession).allow_x);
    CHECK(!enabled_fields(Protocol::telnet, SessionType::xsession).username);
    CHECK(enabled_fields(Protocol::rexec, SessionType::xsession).command);
    CHECK(!enabled_fields(Protocol::vnc, SessionType::xsession).font_server);
    const auto plan9 = enabled_fields(Protocol::drawterm, SessionType::xsession);
    CHECK(plan9.address && plan9.port && plan9.username);
    CHECK(!plan9.key_auth);
    CHECK(!plan9.domain);
    CHECK(!plan9.command);
}

TEST(command_is_asked_for_only_when_the_session_runs_one) {
    CHECK(!enabled_fields(Protocol::ssh_x11, SessionType::xterm).command);
    CHECK(enabled_fields(Protocol::ssh_x11, SessionType::custom).command);
    CHECK(enabled_fields(Protocol::ssh_x11, SessionType::raw).command);
    // The session menu means nothing to other protocols.
    CHECK(!enabled_fields(Protocol::ssh_terminal, SessionType::custom).command);
}

TEST(port_follows_the_protocol_unless_chosen) {
    CHECK_EQ(port_after_change("22", Protocol::telnet), "23");
    CHECK_EQ(port_after_change("", Protocol::vnc), "5900");
    CHECK_EQ(port_after_change("", Protocol::tn3270), "23");
    CHECK_EQ(port_after_change("", Protocol::tn5250), "23");
    CHECK_EQ(port_after_change("", Protocol::drawterm), "17019");
    CHECK_EQ(port_after_change("177", Protocol::rdp), "3389");
    CHECK_EQ(port_after_change("2222", Protocol::telnet), "2222");
    CHECK_EQ(port_after_change("x", Protocol::telnet), "x");
    // A serial console has no port; the field keeps what it had.
    CHECK_EQ(port_after_change("22", Protocol::serial), "22");
}

TEST(a_new_form_has_the_defaults) {
    const auto f = form_for(nullptr);
    CHECK(f.protocol == Protocol::ssh_x11);
    CHECK_EQ(f.port, "22");
    CHECK_EQ(f.device, "/dev/ttyUSB0");
    CHECK_EQ(f.speed, "9600");
    CHECK(f.name.empty());
}

TEST(a_form_shows_the_host) {
    Host h;
    h.name = "VMS";
    h.address = "vms";
    h.protocol = Protocol::ssh_terminal;
    h.port = 2222;
    h.username = "alice";
    h.auth = Auth::key;
    h.allow_x = true;
    const auto f = form_for(&h);
    CHECK_EQ(f.name, "VMS");
    CHECK_EQ(f.port, "2222");
    CHECK_EQ(f.username, "alice");
    CHECK(f.key_auth);
    CHECK(f.allow_x);
    CHECK_EQ(f.device, "/dev/ttyUSB0");
}

TEST(reading_a_form_trims_and_checks_it) {
    auto f = ssh_form();
    f.name = "  Desktop ";
    f.username = " alice";
    f.key_auth = true;
    f.domain = "IGNORED"; // not an RDP host
    const auto h = read_form(f);
    REQUIRE(h.has_value());
    CHECK_EQ(h->name, "Desktop");
    CHECK_EQ(h->username, "alice");
    CHECK_EQ(h->port, 22);
    CHECK(h->auth == Auth::key);
    CHECK(h->domain.empty());
    CHECK(h->id.empty());
}

TEST(each_bad_field_is_named) {
    const auto problem = [](auto change) {
        auto f = ssh_form();
        change(f);
        const auto h = read_form(f);
        return h ? std::string() : h.error();
    };
    CHECK(problem([](HostForm& f) { f.name = ""; }).contains("name"));
    CHECK(problem([](HostForm& f) { f.address = "-oProxy"; }).contains("host name"));
    CHECK(problem([](HostForm& f) { f.port = "0"; }).contains("port"));
    CHECK(problem([](HostForm& f) { f.port = "65536"; }).contains("port"));
    CHECK(problem([](HostForm& f) { f.port = "22x"; }).contains("port"));
    CHECK(problem([](HostForm& f) { f.username = "a b"; }).contains("user name"));
    CHECK(problem([](HostForm& f) {
              f.session = SessionType::custom;
              f.command = "";
          }).contains("command"));
    CHECK(problem([](HostForm& f) { f.font_server = "a b"; }).contains("font server"));
    CHECK(problem([](HostForm& f) {
              f.protocol = Protocol::serial;
              f.device = "/etc/passwd";
          }).contains("serial device"));
    CHECK(problem([](HostForm& f) {
              f.protocol = Protocol::serial;
              f.device = "/dev/ttyUSB0";
              f.speed = "1234";
          }).contains("speed"));
    CHECK(problem([](HostForm& f) {
              f.protocol = Protocol::rdp;
              f.domain = "a b";
          }).contains("domain"));
    CHECK(problem([](HostForm& f) {
              f.protocol = Protocol::rexec;
              f.command = "";
          }).contains("command"));
}

TEST(options_of_other_protocols_are_dropped) {
    auto f = ssh_form();
    f.protocol = Protocol::telnet;
    f.port = "23";
    f.username = "alice";
    f.key_auth = true;
    f.command = "xterm";
    f.allow_x = true;
    const auto h = read_form(f);
    REQUIRE(h.has_value());
    CHECK(h->username.empty());
    CHECK(h->auth == Auth::password);
    CHECK(h->command.empty());
    CHECK(h->allow_x);
}

TEST(a_serial_host_has_a_device_and_no_address) {
    auto f = ssh_form();
    f.protocol = Protocol::serial;
    f.device = "/dev/ttyUSB1";
    f.speed = "115200";
    const auto h = read_form(f);
    REQUIRE(h.has_value());
    CHECK(h->address.empty());
    CHECK_EQ(h->device, "/dev/ttyUSB1");
    CHECK_EQ(h->speed, 115200);
}

TEST(font_servers_are_stored_in_full) {
    auto f = ssh_form();
    f.protocol = Protocol::xdmcp;
    f.port = "177";
    f.indirect = true;
    f.font_server = "fonts";
    const auto h = read_form(f);
    REQUIRE(h.has_value());
    CHECK_EQ(h->font_server, "tcp/fonts:7100");
    CHECK(h->xdmcp_mode == XdmcpMode::indirect);
}

TEST(a_read_form_is_a_valid_settings_host) {
    // Whatever the editor accepts, the settings parser must accept too.
    for (const auto p : all_protocols) {
        auto f = ssh_form();
        f.protocol = p;
        f.command = "xterm -display %D";
        f.device = "/dev/ttyACM0";
        f.speed = "115200";
        const auto h = read_form(f);
        REQUIRE(h.has_value());
        Settings s;
        s.hosts.push_back(*h);
        s.hosts.back().id = "h1";
        const auto back = parse_settings(to_json(s));
        REQUIRE(back.has_value());
        CHECK(back->hosts.front() == s.hosts.front());
    }
}

TEST(list_lines_show_name_address_and_protocol) {
    Host h;
    h.name = "Desktop";
    h.address = "192.0.2.10";
    CHECK_EQ(host_line(h), "Desktop          192.0.2.10      SSH");
    h.protocol = Protocol::serial;
    h.device = "/dev/ttyUSB0";
    CHECK_EQ(host_line(h), "Desktop          ttyUSB0         Serial");
    h.name = "A very long host name indeed";
    CHECK(host_line(h).starts_with("A very long host ttyUSB0"));
}

TEST(protocols_have_menu_and_list_labels) {
    for (const auto p : all_protocols) {
        CHECK(!protocol_menu_label(p).empty());
        CHECK(!protocol_list_label(p).empty());
    }
    CHECK_EQ(protocol_menu_label(Protocol::direct_x), "Direct X (the host opens windows)");
    CHECK_EQ(protocol_list_label(Protocol::ssh_terminal), "Terminal");
    CHECK_EQ(protocol_list_label(Protocol::tn3270), "TN3270");
    CHECK_EQ(protocol_list_label(Protocol::tn5250), "TN5250");
    CHECK_EQ(protocol_list_label(Protocol::drawterm), "Plan 9");
}

TEST(login_asks_for_a_user_name_only_when_needed) {
    Host h;
    CHECK(needs_username(h));
    h.username = "alice";
    CHECK(!needs_username(h));
    h.username.clear();
    h.protocol = Protocol::xdmcp;
    CHECK(!needs_username(h));
}
