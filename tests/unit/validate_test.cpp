// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/validate.hpp>

#include <string>

using namespace pixterm;

TEST(id_allows_letters_digits_dash_and_underscore) {
    CHECK(is_valid_id("h0a1b2c"));
    CHECK(is_valid_id("my-host_2"));
    CHECK(!is_valid_id(""));
    CHECK(!is_valid_id("has space"));
    CHECK(!is_valid_id("dot.ted"));
    CHECK(is_valid_id(std::string(limits::id, 'a')));
    CHECK(!is_valid_id(std::string(limits::id + 1, 'a')));
}

TEST(name_is_any_text_without_control_characters) {
    CHECK(is_valid_name("Lab (SGI Origin 200)"));
    CHECK(!is_valid_name(""));
    CHECK(!is_valid_name("tab\there"));
    CHECK(!is_valid_name("del\x7f"));
    CHECK(!is_valid_name(std::string(limits::name + 1, 'n')));
}

TEST(username_is_portable_and_never_an_option) {
    CHECK(is_valid_username("alice"));
    CHECK(is_valid_username("ALICE"));
    CHECK(is_valid_username("first.last_2-x"));
    CHECK(!is_valid_username(""));
    CHECK(!is_valid_username("-oProxyCommand=x"));
    CHECK(!is_valid_username("a b"));
    CHECK(!is_valid_username("a@b"));
    CHECK(!is_valid_username(std::string(limits::username + 1, 'u')));
}

TEST(domain_is_a_windows_domain) {
    CHECK(is_valid_domain("corp.example"));
    CHECK(is_valid_domain("CORP"));
    CHECK(!is_valid_domain(""));
    CHECK(!is_valid_domain("-x"));
    CHECK(!is_valid_domain("a\\b"));
    CHECK(!is_valid_domain(std::string(limits::domain + 1, 'd')));
}

TEST(address_is_a_host_name_or_ip_literal) {
    CHECK(is_valid_address("desktop"));
    CHECK(is_valid_address("192.0.2.16"));
    CHECK(is_valid_address("fe80::1"));
    CHECK(is_valid_address("host.example.org"));
    CHECK(!is_valid_address(""));
    CHECK(!is_valid_address("-oProxyCommand"));
    CHECK(!is_valid_address(".hidden"));
    CHECK(!is_valid_address("a b"));
    CHECK(!is_valid_address("user@host"));
    CHECK(!is_valid_address(std::string(limits::address + 1, 'a')));
}

TEST(serial_device_is_a_known_port) {
    CHECK(is_valid_serial_device("/dev/ttyUSB0"));
    CHECK(is_valid_serial_device("/dev/ttyACM12"));
    CHECK(is_valid_serial_device("/dev/ttyAMA0"));
    CHECK(is_valid_serial_device("/dev/ttyS1"));
    CHECK(is_valid_serial_device("/dev/serial/by-id/usb-FTDI_FT232R_A10K-if00-port0"));
    CHECK(!is_valid_serial_device("/dev/ttyUSB"));
    CHECK(!is_valid_serial_device("/dev/ttyUSB0x"));
    CHECK(!is_valid_serial_device("/dev/ttyUSB1234"));
    CHECK(!is_valid_serial_device("/dev/sda"));
    CHECK(!is_valid_serial_device("/dev/serial/by-id/"));
    CHECK(!is_valid_serial_device("/dev/serial/by-id/../../sda"));
    CHECK(!is_valid_serial_device("/dev/serial/by-id/a/b"));
}

TEST(serial_speed_is_a_standard_rate) {
    CHECK(is_valid_serial_speed(9600));
    CHECK(is_valid_serial_speed(300));
    CHECK(is_valid_serial_speed(921600));
    CHECK(!is_valid_serial_speed(0));
    CHECK(!is_valid_serial_speed(9601));
    CHECK(!is_valid_serial_speed(-9600));
}

TEST(display_mode_is_auto_or_a_size) {
    CHECK(is_valid_display_mode("auto"));
    CHECK(is_valid_display_mode("1920x1080"));
    CHECK(is_valid_display_mode("1280x1024@75"));
    CHECK(!is_valid_display_mode(""));
    CHECK(!is_valid_display_mode("1920x"));
    CHECK(!is_valid_display_mode("1920x1080@"));
    CHECK(!is_valid_display_mode("1920x1080@60x"));
    CHECK(!is_valid_display_mode("100x100"));
    CHECK(!is_valid_display_mode("9000x1080"));
    CHECK(!is_valid_display_mode("1920x1080@5"));
    CHECK(!is_valid_display_mode("+1920x1080"));
}

TEST(keyboard_layout_and_variant_are_xkb_words) {
    CHECK(is_valid_layout("us"));
    CHECK(is_valid_layout("de"));
    CHECK(!is_valid_layout("u"));
    CHECK(!is_valid_layout("US"));
    CHECK(!is_valid_layout("us;rm"));
    CHECK(is_valid_variant(""));
    CHECK(is_valid_variant("dvorak"));
    CHECK(is_valid_variant("altgr-intl"));
    CHECK(!is_valid_variant("Dvorak"));
}

TEST(ip_is_an_ipv4_or_ipv6_literal) {
    CHECK(is_valid_ip("192.168.1.1"));
    CHECK(is_valid_ip("2001:db8::1"));
    CHECK(!is_valid_ip("192.168.1"));
    CHECK(!is_valid_ip("host"));
    CHECK(!is_valid_ip(""));
}

TEST(ipv4_cidr_needs_a_prefix) {
    CHECK(is_valid_ipv4_cidr("192.168.1.20/24"));
    CHECK(is_valid_ipv4_cidr("10.0.0.1/32"));
    CHECK(!is_valid_ipv4_cidr("192.168.1.20"));
    CHECK(!is_valid_ipv4_cidr("192.168.1.20/"));
    CHECK(!is_valid_ipv4_cidr("192.168.1.20/0"));
    CHECK(!is_valid_ipv4_cidr("192.168.1.20/33"));
    CHECK(!is_valid_ipv4_cidr("192.168.1.20/24x"));
    CHECK(!is_valid_ipv4_cidr("2001:db8::1/64"));
}

TEST(command_is_one_line_of_text) {
    CHECK(is_valid_command("RUN SYS$SYSTEM:DECW$SESSION"));
    CHECK(is_valid_command("xterm -ls -display %D"));
    CHECK(!is_valid_command(""));
    CHECK(!is_valid_command("one\ntwo"));
    CHECK(!is_valid_command(std::string(limits::command + 1, 'c')));
}

TEST(font_server_is_normalized_to_tcp_host_port) {
    CHECK_EQ(normalize_font_server("fs.lan").value_or(""), "tcp/fs.lan:7100");
    CHECK_EQ(normalize_font_server("tcp/fs:7000").value_or(""), "tcp/fs:7000");
    CHECK_EQ(normalize_font_server("fs:7101").value_or(""), "tcp/fs:7101");
    CHECK(!normalize_font_server("fs:0"));
    CHECK(!normalize_font_server("fs:"));
    CHECK(!normalize_font_server("fs:65536"));
    CHECK(!normalize_font_server("-fs"));
    CHECK(!normalize_font_server("fs:7100,evil"));
    CHECK(!normalize_font_server("tcp/"));
    CHECK(!normalize_font_server(""));
}

TEST(only_normalized_font_servers_are_valid) {
    CHECK(is_valid_font_server("tcp/fs:7100"));
    CHECK(!is_valid_font_server("fs"));
    CHECK(!is_valid_font_server("tcp/fs"));
}

TEST(pin_hash_has_the_stored_form) {
    const std::string hash64(64, '0');
    CHECK(is_valid_pin_hash("pbkdf2-sha256$100000$00112233445566778899aabbccddeeff$" + hash64));
    CHECK(!is_valid_pin_hash(""));
    CHECK(!is_valid_pin_hash("pbkdf2-sha256$10$00112233445566778899aabbccddeeff$" + hash64));
    CHECK(!is_valid_pin_hash("pbkdf2-sha256$100000$0011$" + hash64));
    CHECK(!is_valid_pin_hash("pbkdf2-sha256$100000$00112233445566778899aabbccddeeff$00"));
    CHECK(!is_valid_pin_hash("pbkdf2-sha256$100000$00112233445566778899AABBCCDDEEFF$" + hash64));
    CHECK(!is_valid_pin_hash("pbkdf2-sha256$x$00112233445566778899aabbccddeeff$" + hash64));
    CHECK(!is_valid_pin_hash("md5$100000$00112233445566778899aabbccddeeff$" + hash64));
}
