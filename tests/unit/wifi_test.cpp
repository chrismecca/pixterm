// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "test.hpp"

#include <pixterm/wifi.hpp>

using namespace pixterm;

namespace {

// What "wpa_cli scan_results" prints: a header, then tab-separated
// lines, with the SSID escaped as wpa_supplicant does.
constexpr std::string_view scan_results =
    "bssid / frequency / signal level / flags / ssid\n"
    "aa:bb:cc:dd:ee:01\t2437\t-48\t[WPA2-PSK-CCMP][ESS]\thome\n"
    "aa:bb:cc:dd:ee:02\t5180\t-60\t[WPA2-PSK+SAE-CCMP][ESS]\thome\n"
    "aa:bb:cc:dd:ee:03\t2412\t-70\t[WPA2-SAE-CCMP][ESS]\tnew-router\n"
    "aa:bb:cc:dd:ee:04\t2412\t-40\t[ESS]\topen-cafe\n"
    "aa:bb:cc:dd:ee:05\t2412\t-55\t[WPA2-EAP-CCMP][ESS]\tcorp\n"
    "aa:bb:cc:dd:ee:06\t2412\t-65\t[WPA2-PSK-CCMP][ESS]\tcaf\\xc3\\xa9 \\\"5G\\\"\n"
    "aa:bb:cc:dd:ee:07\t2412\t-50\t[WPA2-PSK-CCMP][ESS]\t\n"
    "aa:bb:cc:dd:ee:08\t2412\t-52\t[WPA2-PSK-CCMP][ESS]\t\\x00\\x00\n";

} // namespace

TEST(hex_round_trips_any_bytes) {
    CHECK_EQ(to_hex("home"), "686f6d65");
    CHECK_EQ(from_hex("686f6d65").value_or(""), "home");
    CHECK_EQ(from_hex("686F6D65").value_or(""), "home");
    CHECK(!from_hex("6").has_value());
    CHECK(!from_hex("zz").has_value());
}

TEST(scan_results_list_the_networks_pixterm_can_join) {
    const auto found = parse_scan_results(scan_results);
    REQUIRE_EQ(found.size(), 3U);
    // Strongest first; one entry for each name, with all it offers.
    CHECK_EQ(found[0].ssid, "home");
    CHECK_EQ(found[0].signal, -48);
    CHECK(found[0].wpa2);
    CHECK(found[0].wpa3);
    CHECK_EQ(found[1].ssid, "caf\xc3\xa9 \"5G\"");
    CHECK(found[1].wpa2);
    CHECK(!found[1].wpa3);
    CHECK_EQ(found[2].ssid, "new-router");
    CHECK(!found[2].wpa2);
    CHECK(found[2].wpa3);
}

TEST(scan_replies_carry_names_of_any_bytes) {
    const auto found = parse_scan_results(scan_results);
    const auto reply = scan_reply(found);
    CHECK(reply.contains("-48 wpa2+wpa3 686f6d65\n"));
    const auto back = parse_scan_reply(reply);
    REQUIRE_EQ(back.size(), found.size());
    for (std::size_t i = 0; i < back.size(); ++i) {
        CHECK_EQ(back[i].ssid, found[i].ssid);
        CHECK_EQ(back[i].signal, found[i].signal);
        CHECK(back[i].wpa2 == found[i].wpa2);
        CHECK(back[i].wpa3 == found[i].wpa3);
    }
    CHECK(parse_scan_reply("junk\n-50 wpa2 zz\n").empty());
}

TEST(wpa_supplicant_conf_joins_wpa2_or_wpa3) {
    Wifi w{.enabled = true, .ssid = "caf\xc3\xa9", .hidden = true, .country = "FR"};
    const auto conf = wpa_supplicant_conf(w, "pass\"word1");
    CHECK(conf.contains("country=FR\n"));
    CHECK(conf.contains("ctrl_interface=DIR=/run/wpa_supplicant GROUP=root\n"));
    CHECK(conf.contains("\tssid=636166c3a9\n"));
    CHECK(conf.contains("\tscan_ssid=1\n"));
    CHECK(conf.contains("\tkey_mgmt=WPA-PSK WPA-PSK-SHA256 SAE\n"));
    CHECK(conf.contains("\tieee80211w=1\n"));
    CHECK(conf.contains("\tpsk=\"pass\"word1\"\n"));
    CHECK(conf.contains("\tsae_password=\"pass\"word1\"\n"));

    w.hidden = false;
    CHECK(!wpa_supplicant_conf(w, "password1").contains("scan_ssid"));
    // Only scanning: no network to join.
    CHECK(!wpa_supplicant_conf(w, "").contains("network="));
    w.enabled = false;
    CHECK(!wpa_supplicant_conf(w, "password1").contains("network="));
}
