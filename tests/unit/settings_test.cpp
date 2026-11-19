// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/settings.hpp>

#include <format>
#include <string>

using namespace pixterm;

namespace {

// Parses settings that must be valid; on failure, records why.
Settings parse_ok(std::string_view text) {
    auto s = parse_settings(text);
    if (!s) {
        pixterm::test::fail("parse_settings", s.error());
        return {};
    }
    return *s;
}

bool rejects(std::string_view text) {
    const auto s = parse_settings(text);
    return !s && !s.error().empty();
}

// A settings document holding one host with the given JSON members.
std::string with_host(std::string_view members) {
    return std::string(R"({"schema_version":1,"hosts":[{"id":"a","name":"n",)")
           + std::string(members) + "}]}";
}

} // namespace

TEST(defaults_when_only_the_version_is_given) {
    const auto s = parse_ok(R"({"schema_version":1})");
    CHECK(s.hosts.empty());
    CHECK_EQ(s.display_mode, "auto");
    CHECK_EQ(s.blank_minutes, 10);
    CHECK_EQ(s.keyboard_layout, "us");
    CHECK_EQ(s.keyboard_variant, "");
    CHECK(!s.network.is_static);
    CHECK(!s.wifi.enabled);
    CHECK_EQ(s.generation, 0U);
}

TEST(reads_device_settings) {
    const auto s = parse_ok(R"({"schema_version":1,"device":{
        "display":{"mode":"1920x1080@60","blank_minutes":0},
        "keyboard":{"layout":"de","variant":"nodeadkeys"},
        "network":{"method":"static","address":"192.0.2.250/24",
                   "gateway":"192.0.2.1","dns":["192.0.2.1","1.1.1.1"]}}})");
    CHECK_EQ(s.display_mode, "1920x1080@60");
    CHECK_EQ(s.blank_minutes, 0);
    CHECK_EQ(s.keyboard_layout, "de");
    CHECK_EQ(s.keyboard_variant, "nodeadkeys");
    CHECK(s.network.is_static);
    CHECK_EQ(s.network.address, "192.0.2.250/24");
    CHECK_EQ(s.network.gateway, "192.0.2.1");
    REQUIRE_EQ(s.network.dns.size(), 2U);
    CHECK_EQ(s.network.dns[1], "1.1.1.1");
}

TEST(reads_x11_hosts) {
    const auto s = parse_ok(R"({"schema_version":1,"hosts":[
        {"id":"a","name":"xdmhost","address":"192.0.2.16","protocol":"xdmcp",
         "xdmcp_mode":"indirect","font_server":"xdmhost"},
        {"id":"b","name":"desktop","address":"desktop.example.org","protocol":"ssh-x11",
         "username":"alice","session":"xsession","auth":"key"},
        {"id":"c","name":"vms","address":"192.0.2.10","protocol":"ssh-x11",
         "username":"ALICE","session":"raw","command":"RUN SYS$SYSTEM:DECW$SESSION"}]})");
    REQUIRE_EQ(s.hosts.size(), 3U);
    const auto& xdmhost = s.hosts[0];
    CHECK(xdmhost.protocol == Protocol::xdmcp);
    CHECK_EQ(xdmhost.port, 177);
    CHECK(xdmhost.xdmcp_mode == XdmcpMode::indirect);
    CHECK_EQ(xdmhost.font_server, "tcp/xdmhost:7100");
    const auto& desktop = s.hosts[1];
    CHECK_EQ(desktop.port, 22);
    CHECK(desktop.session == SessionType::xsession);
    CHECK(desktop.auth == Auth::key);
    const auto& vms = s.hosts[2];
    CHECK(vms.session == SessionType::raw);
    CHECK(vms.auth == Auth::password);
    CHECK_EQ(vms.command, "RUN SYS$SYSTEM:DECW$SESSION");
    CHECK(s.find_host("b") == &s.hosts[1]);
    CHECK(s.find_host("z") == nullptr);
}

TEST(terminal_hosts_take_ssh_options_but_not_x11_ones) {
    const auto s = parse_ok(R"({"schema_version":1,"hosts":[
        {"id":"t1","name":"vms","address":"vms","protocol":"ssh-terminal",
         "username":"ALICE","auth":"key","session":"xterm","font_server":"fs"},
        {"id":"t2","name":"old","address":"10.0.0.9","protocol":"telnet",
         "username":"ignored","auth":"key"}]})");
    REQUIRE_EQ(s.hosts.size(), 2U);
    CHECK(s.hosts[0].protocol == Protocol::ssh_terminal);
    CHECK_EQ(s.hosts[0].username, "ALICE");
    CHECK(s.hosts[0].auth == Auth::key);
    CHECK_EQ(s.hosts[0].font_server, "");
    CHECK(s.hosts[1].protocol == Protocol::telnet);
    CHECK_EQ(s.hosts[1].port, 23);
    CHECK_EQ(s.hosts[1].username, "");
    CHECK(s.hosts[1].auth == Auth::password);
    const auto text = to_json(s);
    CHECK(!text.contains("\"session\""));
    CHECK(!text.contains("font_server"));
}

TEST(direct_x_hosts) {
    const auto s = parse_ok(R"({"schema_version":1,"hosts":[
        {"id":"d1","name":"genera","address":"lispm","protocol":"direct-x",
         "allow_x":true,"username":"x"},
        {"id":"d2","name":"sgi","address":"indy","protocol":"rexec",
         "username":"guest","command":"xterm -display %D"},
        {"id":"d3","name":"sun","address":"sparc","protocol":"rsh",
         "command":"xclock -display %D"},
        {"id":"d4","name":"vms","address":"vms","protocol":"telnet","allow_x":true}]})");
    REQUIRE_EQ(s.hosts.size(), 4U);
    CHECK(!s.hosts[0].allow_x);
    CHECK_EQ(s.hosts[0].username, "");
    CHECK(s.hosts[0].opens_windows_directly());
    CHECK_EQ(s.hosts[1].port, 512);
    CHECK_EQ(s.hosts[1].command, "xterm -display %D");
    CHECK_EQ(s.hosts[1].username, "guest");
    CHECK_EQ(s.hosts[2].port, 514);
    CHECK(s.hosts[3].allow_x);
    CHECK(s.hosts[3].opens_windows_directly());
    CHECK(rejects(with_host(R"("address":"h","protocol":"rexec")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"telnet","allow_x":"yes")")));
}

TEST(remote_desktop_hosts) {
    const auto s = parse_ok(R"({"schema_version":1,"hosts":[
        {"id":"v1","name":"mac","address":"imac","protocol":"vnc",
         "username":"ignored","domain":"IGNORED"},
        {"id":"r1","name":"win","address":"nt","protocol":"rdp",
         "username":"chris","domain":"LAB"}]})");
    REQUIRE_EQ(s.hosts.size(), 2U);
    CHECK_EQ(s.hosts[0].port, 5900);
    CHECK_EQ(s.hosts[0].username, "");
    CHECK_EQ(s.hosts[0].domain, "");
    CHECK_EQ(s.hosts[1].port, 3389);
    CHECK_EQ(s.hosts[1].domain, "LAB");
    CHECK(rejects(with_host(R"("address":"h","protocol":"rdp","domain":"BAD DOMAIN")")));
}

TEST(serial_hosts_have_a_device_instead_of_an_address) {
    const auto s = parse_ok(R"({"schema_version":1,"hosts":[
        {"id":"s1","name":"sun","protocol":"serial","device":"/dev/ttyUSB0"},
        {"id":"s2","name":"vax","protocol":"serial","speed":19200,"address":"ignored",
         "device":"/dev/serial/by-id/usb-FTDI_FT232R-if00-port0"}]})");
    REQUIRE_EQ(s.hosts.size(), 2U);
    CHECK_EQ(s.hosts[0].speed, 9600);
    CHECK_EQ(s.hosts[0].device, "/dev/ttyUSB0");
    CHECK_EQ(s.hosts[0].address, "");
    CHECK_EQ(s.hosts[1].speed, 19200);
    CHECK_EQ(s.hosts[1].address, "");
    CHECK(rejects(with_host(R"("protocol":"serial","device":"/etc/passwd")")));
    CHECK(rejects(with_host(R"("protocol":"serial","device":"/dev/ttyUSB0","speed":12345)")));
    CHECK(rejects(with_host(R"("protocol":"serial")")));
}

TEST(round_trips_through_json) {
    const auto first = parse_ok(R"({"schema_version":1,
        "device":{"display":{"mode":"1280x1024@75","blank_minutes":30},
                  "wifi":{"enabled":true,"ssid":"Caf\u00e9","hidden":false,"country":"FR"},
                  "keyboard":{"layout":"de","variant":"nodeadkeys"},
                  "network":{"method":"static","address":"10.0.0.2/24","dns":["10.0.0.1"]}},
        "hosts":[
        {"id":"a","name":"xdmhost","address":"i","protocol":"xdmcp","xdmcp_mode":"indirect",
         "font_server":"tcp/fs:7101"},
        {"id":"b","name":"desktop","address":"e","protocol":"ssh-x11","username":"alice",
         "session":"custom","command":"xterm -ls","auth":"key"},
        {"id":"c","name":"t","address":"t","protocol":"telnet","allow_x":true,"port":2323},
        {"id":"d","name":"r","address":"r","protocol":"rsh","username":"u","command":"xclock"},
        {"id":"e","name":"w","address":"w","protocol":"rdp","username":"","domain":"LAB"},
        {"id":"f","name":"s","protocol":"serial","device":"/dev/ttyS0","speed":115200}]})");
    const auto again = parse_ok(to_json(first));
    CHECK(again == first);
}

TEST(rejects_bad_settings_whole) {
    CHECK(rejects("not json"));
    CHECK(rejects("[]"));
    CHECK(rejects(R"({"schema_version":2})"));
    CHECK(rejects(R"({})"));
    CHECK(rejects(R"({"schema_version":1,"hosts":{}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"admin_pin":"1234"}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"network":{"method":"static",
                     "address":"10.0.0.1"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"network":{"method":"ppp"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"network":{"method":"static",
                     "address":"10.0.0.1/24","dns":["a","b","c"]}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"display":{"mode":"99999x1"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"display":{"blank_minutes":-1}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"display":{"blank_minutes":121}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"display":{"blank_minutes":"10"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"keyboard":{"layout":"us;rm"}}})"));
}

TEST(reads_wifi_settings) {
    const auto s = parse_ok(R"({"schema_version":1,"device":{"wifi":{
        "enabled":true,"ssid":"home","hidden":true,"country":"GB"}}})");
    CHECK(s.wifi.enabled);
    CHECK_EQ(s.wifi.ssid, "home");
    CHECK(s.wifi.hidden);
    CHECK_EQ(s.wifi.country, "GB");
    // Off, it may keep the network it last used.
    CHECK(!parse_ok(R"({"schema_version":1,"device":{"wifi":{"enabled":false,"ssid":"home"}}})")
               .wifi.enabled);
}

TEST(rejects_bad_wifi_settings) {
    // On, it needs a network and a country.
    CHECK(rejects(R"({"schema_version":1,"device":{"wifi":{"enabled":true,"country":"GB"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"wifi":{"enabled":true,"ssid":"home"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"wifi":{"enabled":"yes"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"wifi":{"country":"Britain"}}})"));
    CHECK(rejects(R"({"schema_version":1,"device":{"wifi":{"ssid":""}}})") == false);
    // The passphrase never belongs in the settings.
    CHECK(rejects(R"({"schema_version":1,"device":{"wifi":{"passphrase":"secret12"}}})"));
}

TEST(hosts_may_be_woken_by_their_mac_address) {
    const auto s =
        parse_ok(with_host(R"("address":"h","protocol":"ssh-x11","wake_mac":"00-1A-2B-3C-4D-5E")"));
    REQUIRE_EQ(s.hosts.size(), 1U);
    CHECK_EQ(s.hosts[0].wake_mac, "00:1a:2b:3c:4d:5e");
    CHECK(rejects(with_host(R"("address":"h","protocol":"ssh-x11","wake_mac":"nonsense")")));
    CHECK(parse_ok(to_json(s)) == s);
}

TEST(rejects_bad_hosts) {
    CHECK(rejects(with_host(R"("address":"h","protocol":"ssh-x11","auth":"hostbased")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"nx")")));
    CHECK(rejects(with_host(R"("address":"h")")));
    CHECK(rejects(with_host(R"("address":"-oProxyCommand=x","protocol":"ssh-x11")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"ssh-x11","username":"-l")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"ssh-x11","session":"custom")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"ssh-x11","session":"raw",
                               "command":"a\nb")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"ssh-x11","session":"xfce")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"xdmcp","xdmcp_mode":"chooser")")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"xdmcp","port":70000)")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"xdmcp","port":22.5)")));
    CHECK(rejects(with_host(R"("address":"h","protocol":"xdmcp","font_server":"-x")")));
    CHECK(rejects(R"({"schema_version":1,"hosts":[
        {"id":"a","name":"n","address":"h","protocol":"xdmcp"},
        {"id":"a","name":"m","address":"h","protocol":"xdmcp"}]})"));
    CHECK(rejects(R"({"schema_version":1,"hosts":[1]})"));
    std::string many = R"({"schema_version":1,"hosts":[)";
    for (std::size_t i = 0; i <= max_hosts; ++i)
        many += std::format(R"({}{{"id":"h{}","name":"n","address":"h","protocol":"xdmcp"}})",
                            i == 0 ? "" : ",", i);
    CHECK(rejects(many + "]}"));
}

TEST(protocol_traits) {
    CHECK(is_ssh(Protocol::ssh_terminal));
    CHECK(!is_ssh(Protocol::telnet));
    CHECK(is_x11(Protocol::xdmcp));
    CHECK(!is_x11(Protocol::ssh_terminal));
    CHECK(wants_user(Protocol::rsh));
    CHECK(wants_user(Protocol::rdp));
    CHECK(!wants_user(Protocol::direct_x));
    CHECK(is_terminal(Protocol::telnet));
    CHECK(!is_terminal(Protocol::serial));
    CHECK_EQ(protocol_name(Protocol::direct_x), "direct-x");
    CHECK(protocol_from_name("ssh-terminal") == Protocol::ssh_terminal);
    CHECK(!protocol_from_name("rlogin"));
    for (const auto p : all_protocols)
        CHECK(protocol_from_name(protocol_name(p)) == p);
}

TEST(raw_commands_are_sent_unchanged) {
    Host h;
    h.session = SessionType::raw;
    h.command = "RUN SYS$SYSTEM:DECW$SESSION";
    CHECK_EQ(remote_command(h), "RUN SYS$SYSTEM:DECW$SESSION");
}

TEST(custom_commands_are_quoted_data_for_sh) {
    Host h;
    h.session = SessionType::custom;
    h.command = "echo 'it''s' $HOME";
    const auto cmd = remote_command(h);
    CHECK(cmd.contains(R"('echo '\''it'\'''\''s'\'' $HOME')"));
    CHECK(cmd.starts_with("exec /bin/sh -c '"));
}

TEST(preset_sessions_check_for_x11_forwarding) {
    Host h;
    h.session = SessionType::xsession;
    CHECK(remote_command(h).contains(".xsession"));
    CHECK(remote_command(h).contains("X11 forwarding is not enabled"));
    h.session = SessionType::xterm;
    CHECK(remote_command(h).contains("exec xterm"));
}

TEST(new_host_ids_are_valid_and_differ) {
    const auto a = new_host_id();
    const auto b = new_host_id();
    CHECK(is_valid_id(a));
    CHECK(a != b);
}

TEST(network_problems_are_explained) {
    Network n;
    CHECK(!network_problem(n));
    n.is_static = true;
    n.address = "10.0.0.2";
    CHECK(network_problem(n).has_value());
    n.address = "10.0.0.2/24";
    CHECK(!network_problem(n));
    n.gateway = "fe80::1";
    CHECK(network_problem(n).has_value());
    n.gateway = "10.0.0.1";
    n.dns = {"name.server"};
    CHECK(network_problem(n).has_value());
}
