// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "temp_dir.hpp"
#include "test.hpp"

#include <pixterm/pin.hpp>

#include <host_list.hpp>
#include <messages.hpp>
#include <panels.hpp>
#include <pin_gate.hpp>
#include <system_info.hpp>

#include <fstream>

using namespace pixterm;
using namespace pixterm::chooser;
using pixterm::test::TempDir;

namespace {

Settings three_hosts() {
    Settings s;
    for (const auto* name : {"a", "b", "c"}) {
        Host h;
        h.id = name;
        h.name = name;
        h.address = name;
        s.hosts.push_back(h);
    }
    return s;
}

std::string names(const Settings& s) {
    std::string out;
    for (const auto& h : s.hosts)
        out += h.name;
    return out;
}

// The message a line holds, if it is of type T.
template <typename T>
std::optional<T> message_as(std::string_view line) {
    const auto m = parse_message(line);
    if (!m || !std::holds_alternative<T>(*m))
        return std::nullopt;
    return std::get<T>(*m);
}

Monitor monitor() {
    Monitor m;
    m.output = "HDMI-1";
    m.name = "PANEL-1";
    m.modes = {{.id = 1, .width = 1920, .height = 1080, .hz = 60, .preferred = true},
               {.id = 2, .width = 1280, .height = 1024, .hz = 60, .preferred = false},
               {.id = 3, .width = 1024, .height = 768, .hz = 60, .preferred = false}};
    m.current = 1;
    return m;
}

} // namespace

// ---- messages ----

TEST(supervisor_messages_are_read) {
    CHECK(message_as<Handoff>("HANDOFF").has_value());
    CHECK(message_as<Idle>("IDLE").has_value());
    CHECK_EQ(message_as<Error>("ERROR Could not log in.\\n\\nNo route").value_or(Error{}).text,
             "Could not log in.\n\nNo route");
    const auto applied = message_as<Applied>("APPLIED");
    REQUIRE(applied.has_value());
    CHECK(applied->text.empty());
    CHECK_EQ(message_as<Applied>("APPLIED ssh-ed25519 AAAA").value_or(Applied{}).text,
             "ssh-ed25519 AAAA");
    CHECK_EQ(message_as<ApplyFailed>("APPLY_FAILED No drive").value_or(ApplyFailed{}).text,
             "No drive");
    CHECK(!parse_message("HELLO").has_value());
    CHECK(!parse_message("IDLEX").has_value());
    CHECK(!parse_message("").has_value());
}

TEST(requests_are_formatted) {
    CHECK_EQ(start_request("h1", ""), "START h1 -");
    CHECK_EQ(start_request("h1", "alice"), "START h1 alice");
    CHECK_EQ(query_request("192.0.2.5", "Lab host"), "QUERY 192.0.2.5 Lab host");
    CHECK_EQ(query_request("192.0.2.5", "two\nlines"), "QUERY 192.0.2.5 two\\nlines");
    CHECK_EQ(request_line(Request::cancel), "CANCEL");
    CHECK_EQ(request_line(Request::apply), "APPLY");
    CHECK_EQ(request_line(Request::reset), "RESET");
    CHECK_EQ(request_line(Request::export_settings), "EXPORT");
    CHECK_EQ(request_line(Request::import_settings), "IMPORT");
    CHECK_EQ(request_line(Request::keygen), "KEYGEN");
    CHECK_EQ(request_line(Request::reboot), "REBOOT");
}

// ---- host list edits ----

TEST(adding_a_host_gives_it_an_id_and_selects_it) {
    Host h;
    h.name = "d";
    h.address = "d";
    const auto e = add_host(three_hosts(), h);
    REQUIRE(e.has_value());
    CHECK_EQ(names(e->settings), "abcd");
    CHECK_EQ(e->select, 3U);
    CHECK(is_valid_id(e->settings.hosts[3].id));
    CHECK(e->settings.hosts[3].id != "c");
}

TEST(the_host_list_has_a_limit) {
    Settings s;
    s.hosts.resize(max_hosts);
    CHECK(!add_host(s, Host{}).has_value());
}

TEST(editing_a_host_keeps_its_id) {
    Host h;
    h.name = "B2";
    h.address = "b2";
    const auto e = replace_host(three_hosts(), 1, h);
    CHECK_EQ(names(e.settings), "aB2c");
    CHECK_EQ(e.settings.hosts[1].id, "b");
    CHECK_EQ(e.select, 1U);
}

TEST(removing_a_host_selects_its_neighbour) {
    auto e = remove_host(three_hosts(), 1);
    CHECK_EQ(names(e.settings), "ac");
    CHECK_EQ(e.select, 0U);
    e = remove_host(three_hosts(), 0);
    CHECK_EQ(names(e.settings), "bc");
    CHECK_EQ(e.select, 0U);
}

TEST(hosts_move_up_and_down_within_the_list) {
    auto e = move_host(three_hosts(), 1, -1);
    REQUIRE(e.has_value());
    CHECK_EQ(names(e->settings), "bac");
    CHECK_EQ(e->select, 0U);
    e = move_host(three_hosts(), 1, 1);
    REQUIRE(e.has_value());
    CHECK_EQ(names(e->settings), "acb");
    CHECK(!move_host(three_hosts(), 0, -1).has_value());
    CHECK(!move_host(three_hosts(), 2, 1).has_value());
}

TEST(list_buttons_follow_the_selection) {
    auto b = list_buttons(3, std::nullopt);
    CHECK(!b.edit);
    CHECK(!b.remove);
    CHECK(!b.up);
    CHECK(!b.down);
    b = list_buttons(3, 0);
    CHECK(b.edit);
    CHECK(b.remove);
    CHECK(!b.up);
    CHECK(b.down);
    b = list_buttons(3, 2);
    CHECK(b.up);
    CHECK(!b.down);
}

TEST(a_found_host_is_listed_once) {
    auto s = three_hosts();
    s.hosts[1].protocol = Protocol::xdmcp;
    CHECK(has_xdmcp_host(s, "b"));
    CHECK(!has_xdmcp_host(s, "a")); // an SSH host at that address is another entry
}

// ---- the administrator PIN ----

TEST(no_pin_means_no_gate) {
    PinGate gate;
    CHECK(!gate.needs_pin(""));
    CHECK(gate.needs_pin("pbkdf2-sha256$..."));
    gate.unlock();
    CHECK(!gate.needs_pin("pbkdf2-sha256$..."));
}

TEST(the_right_pin_unlocks) {
    const auto hash = make_pin_hash("2468");
    REQUIRE(hash.has_value());
    PinGate gate;
    const Time t0{};
    CHECK(gate.enter("1111", *hash, t0) == PinGate::Result::wrong);
    CHECK(gate.enter("2468", *hash, t0) == PinGate::Result::unlocked);
    CHECK(!gate.needs_pin(*hash));
}

TEST(three_wrong_pins_lock_the_gate_for_a_while) {
    const auto hash = make_pin_hash("2468");
    REQUIRE(hash.has_value());
    PinGate gate;
    const Time t0{};
    for (int i = 0; i < PinGate::tries; ++i)
        CHECK(gate.enter("0000", *hash, t0) == PinGate::Result::wrong);
    CHECK(gate.enter("2468", *hash, t0 + std::chrono::seconds(5)) == PinGate::Result::locked_out);
    CHECK(gate.enter("2468", *hash, t0 + PinGate::lockout) == PinGate::Result::unlocked);
}

TEST(a_new_pin_must_be_valid_and_typed_twice) {
    CHECK(new_pin_hash("12", "12").error().contains("4 to 12 digits"));
    CHECK(new_pin_hash("1234", "1235").error().contains("do not match"));
    const auto hash = new_pin_hash("1234", "1234");
    REQUIRE(hash.has_value());
    CHECK(verify_pin("1234", *hash));
}

// ---- Configure panels ----

TEST(display_rows_start_with_automatic) {
    const auto m = monitor();
    const auto rows = mode_rows(m);
    REQUIRE_EQ(rows.size(), 4U);
    CHECK(rows[0].starts_with("Automatic"));
    CHECK(rows[0].contains("1920 x 1080"));
    CHECK_EQ(rows[2], mode_label(m.modes[1]));
    CHECK_EQ(mode_row(m, "auto"), 0U);
    CHECK_EQ(mode_row(m, "1024x768@60"), 3U);
    CHECK_EQ(mode_row(m, "640x480"), 0U);
}

TEST(choosing_a_display_row_names_the_setting_and_the_mode) {
    const auto m = monitor();
    auto pick = pick_mode(m, 0);
    CHECK_EQ(pick.setting, "auto");
    CHECK(pick.mode == std::optional<std::size_t>(0));
    pick = pick_mode(m, 2);
    CHECK_EQ(pick.setting, "1280x1024@60");
    CHECK(pick.mode == std::optional<std::size_t>(1));
}

TEST(display_facts_are_described) {
    auto m = monitor();
    CHECK_EQ(monitor_text(m), "Monitor:  PANEL-1 on HDMI-1");
    CHECK(current_mode_text(m).contains("1280 x 1024"));
    m.name.clear();
    m.current.reset();
    CHECK_EQ(monitor_text(m), "Monitor:  unknown model on HDMI-1");
    CHECK_EQ(current_mode_text(m), "Current mode:  unknown");
    CHECK_EQ(monitor_text(std::nullopt), "Monitor:  none detected");
}

TEST(the_mode_question_counts_down) {
    CHECK(keep_mode_question(15).contains("in 15 seconds."));
    CHECK(keep_mode_question(1).contains("in 1 second."));
}

TEST(blanking_times_are_offered_from_never_to_an_hour) {
    CHECK_EQ(blank_choices.front(), 0);
    CHECK_EQ(blank_choices.back(), 60);
    CHECK_EQ(blank_label(0), "Never");
    CHECK_EQ(blank_label(1), "1 minute");
    CHECK_EQ(blank_label(10), "10 minutes");
}

TEST(a_saved_blanking_time_shows_as_the_nearest_choice) {
    for (std::size_t i = 0; i < blank_choices.size(); ++i)
        CHECK_EQ(blank_row(blank_choices.at(i)), i);
    CHECK_EQ(blank_choices.at(blank_row(20)), 15); // edited by hand
    CHECK_EQ(blank_choices.at(blank_row(120)), 60);
    CHECK_EQ(blank_choices.at(blank_row(1)), 0);
    CHECK_EQ(blank_choices.at(blank_row(3)), 5);
}

TEST(keyboard_layouts_are_listed_by_code) {
    REQUIRE(!keyboard_layouts.empty());
    CHECK_EQ(keyboard_layouts[0].code, "us");
    CHECK(layout_row("gb").has_value());
    CHECK(!layout_row("zz").has_value());
    CHECK_EQ(layout_line(keyboard_layouts[0]), "us     English (US)");
    for (const auto& l : keyboard_layouts)
        CHECK(is_valid_layout(l.code));
}

TEST(the_network_form_reads_into_settings) {
    NetworkForm f;
    CHECK(read_network_form(f).has_value());
    f.is_static = true;
    f.address = " 192.0.2.20/24 ";
    f.gateway = "192.0.2.1";
    f.dns2 = "192.0.2.53";
    const auto n = read_network_form(f);
    REQUIRE(n.has_value());
    CHECK_EQ(n->address, "192.0.2.20/24");
    CHECK(n->dns == std::vector<std::string>{"192.0.2.53"});
    f.address = "192.0.2.20";
    CHECK(!read_network_form(f).has_value());

    Network shown;
    shown.is_static = true;
    shown.dns = {"192.0.2.1", "192.0.2.2"};
    const auto back = network_form(shown);
    CHECK(back.is_static);
    CHECK_EQ(back.dns2, "192.0.2.2");
}

TEST(a_variant_is_checked) {
    CHECK(read_keyboard("us", " dvorak ").has_value());
    CHECK_EQ(read_keyboard("us", "").value().second, "");
    CHECK(!read_keyboard("us", "Not Valid").has_value());
}

// ---- system facts ----

TEST(os_name_comes_from_os_release) {
    CHECK_EQ(os_name("ID=debian\nPRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\n"),
             "Debian GNU/Linux 13 (trixie)");
    CHECK_EQ(os_name("PRETTY_NAME=Plain\n"), "Plain");
    CHECK_EQ(os_name("NAME=x\n"), "");
}

TEST(uptime_is_days_hours_and_minutes) {
    CHECK_EQ(uptime_text("93784.52 1000.0\n"), "1 day, 2:03");
    CHECK_EQ(uptime_text("300000 1"), "3 days, 11:20");
    CHECK_EQ(uptime_text("59"), "0 days, 0:00");
    CHECK_EQ(uptime_text("junk"), "");
}

TEST(version_shows_the_build) {
    CHECK_EQ(version_text("1.0.0", ""), "1.0.0");
    CHECK_EQ(version_text("1.0.0", "abc123"), "1.0.0 (abc123)");
}

TEST(settings_generation_is_described) {
    CHECK_EQ(settings_text(0), "factory defaults");
    CHECK_EQ(settings_text(7), "generation 7");
}

TEST(network_status_shows_the_address) {
    CHECK_EQ(network_status({}), "Network: Not connected");
    CHECK_EQ(network_status(Address{.interface = "eth0", .ipv4 = "192.0.2.20"}),
             "Network: 192.0.2.20");
}

TEST(first_line_reads_sysfs_style_files) {
    const TempDir dir;
    std::ofstream(dir.path() / "model") << std::string("Board Model 3\0", 14);
    std::ofstream(dir.path() / "address") << "b8:27:eb:00:00:01\n";
    CHECK_EQ(first_line(dir.path() / "model"), "Board Model 3");
    CHECK_EQ(first_line(dir.path() / "address"), "b8:27:eb:00:00:01");
    CHECK_EQ(first_line(dir.path() / "missing"), "");
}
