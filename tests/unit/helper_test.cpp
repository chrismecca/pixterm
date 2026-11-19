// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "temp_dir.hpp"
#include "test.hpp"

#include <pixterm/pin.hpp>
#include <pixterm/settings_store.hpp>
#include <pixterm/wifi.hpp>

#include <helper.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>

using namespace pixterm;
using namespace pixterm::helper;
using pixterm::test::TempDir;
namespace fs = std::filesystem;

namespace {

// The privileged operations, done in a temporary directory instead.
class FakePrivileged final : public Privileged {
public:
    fs::path usb_source; ///< what "mounting" the drive shows
    fs::path usb_dir;
    bool writable = false;
    int writes = 0;
    std::vector<std::vector<std::string>> ran;
    std::vector<fs::path> handed_over;

    std::expected<void, std::string> begin_writing() override {
        writable = true;
        ++writes;
        return {};
    }
    void finish_writing() override { writable = false; }

    int run(const std::vector<std::string>& argv) override {
        ran.push_back(argv);
        // ssh-keygen -f FILE makes FILE and FILE.pub.
        if (argv.front().ends_with("ssh-keygen")) {
            const fs::path key = argv.back();
            std::ofstream(key) << "PRIVATE";
            std::ofstream(key.string() + ".pub") << "ssh-ed25519 AAAA pixterm@test\n";
        }
        return 0;
    }

    std::expected<std::string, std::string> mount_usb() override {
        if (usb_source.empty())
            return std::unexpected("No USB drive was found.");
        fs::create_directories(usb_dir);
        fs::copy(usb_source, usb_dir,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        return "sda1";
    }
    void unmount_usb() override {
        if (!usb_source.empty()) {
            fs::copy(usb_dir, usb_source,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            fs::remove_all(usb_dir);
        }
    }

    std::vector<std::string> ethernet_interfaces() override { return {"eth0"}; }

    std::string wifi = "wlan0";
    std::string scan_output;
    int pauses = 0;
    std::optional<std::string> wifi_interface() override {
        return wifi.empty() ? std::nullopt : std::optional(wifi);
    }
    std::optional<std::string> run_output(const std::vector<std::string>& argv) override {
        ran.push_back(argv);
        if (argv.back() == "scan_results")
            return scan_output;
        return argv.back() == "ping" ? "PONG\n" : "OK\n";
    }
    void pause(std::chrono::milliseconds /*time*/) override { ++pauses; }
    bool hand_to_service(const fs::path& path) override {
        handed_over.push_back(path);
        return true;
    }
};

Paths paths_in(const fs::path& dir) {
    return {.state_dir = dir / "state",
            .factory_settings = dir / "factory.json",
            .network_dir = dir / "network",
            .usb_dir = dir / "usb",
            .keygen_dir = dir / "keygen",
            .wifi_dir = dir / "wifi",
            .xorg_log = dir / "Xorg.log"};
}

struct Rig {
    TempDir dir;
    FakePrivileged privileged;
    Helper helper;

    Rig() : helper(paths_in(dir.path()), privileged) {
        fs::create_directories(dir.path() / "state");
        fs::create_directories(dir.path() / "usb-drive");
        privileged.usb_dir = dir.path() / "usb";
    }

    Reply ask(std::vector<std::string> args, std::string payload = {}) {
        return helper.handle({.args = std::move(args), .payload = std::move(payload)});
    }

    [[nodiscard]] fs::path state(std::string_view name) const {
        return dir.path() / "state" / name;
    }

    [[nodiscard]] fs::path wifi_conf() const { return dir.path() / "wifi/wpa_supplicant.conf"; }

    [[nodiscard]] bool ran(const std::vector<std::string>& argv) const {
        return std::ranges::find(privileged.ran, argv) != privileged.ran.end();
    }

    [[nodiscard]] Settings saved() const {
        return SettingsStore(dir.path() / "state", dir.path() / "factory.json").load().settings;
    }
};

std::vector<std::string> words(std::initializer_list<const char*> list) {
    return {list.begin(), list.end()};
}

std::string read(const fs::path& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}

} // namespace

TEST(network_files_for_dhcp_and_static_addresses) {
    const auto dhcp = network_file({});
    CHECK(dhcp.contains("DHCP=ipv4"));
    CHECK(dhcp.contains("ClientIdentifier=mac"));
    const auto fixed = network_file({.is_static = true,
                                     .address = "10.0.0.2/24",
                                     .gateway = "10.0.0.1",
                                     .dns = {"10.0.0.1", "1.1.1.1"}});
    CHECK(fixed.contains("Address=10.0.0.2/24\n"));
    CHECK(fixed.contains("Gateway=10.0.0.1\n"));
    CHECK(fixed.contains("DNS=1.1.1.1\n"));
    CHECK(!fixed.contains("DHCP"));
}

TEST(network_arguments_are_checked) {
    CHECK(parse_network_args(words({"dhcp"})).has_value());
    const auto n = parse_network_args(words({"static", "10.0.0.2/24", "-", "8.8.8.8", "-"}));
    REQUIRE(n.has_value());
    CHECK(n->is_static);
    CHECK_EQ(n->gateway, "");
    CHECK(n->dns == std::vector<std::string>{"8.8.8.8"});
    CHECK(!parse_network_args(words({"static", "10.0.0.2", "-", "-", "-"})));
    CHECK(!parse_network_args(words({"static", "10.0.0.2/24"})));
    CHECK(!parse_network_args(words({"ppp"})));
    CHECK(!parse_network_args(words({})));
}

TEST(known_hosts_must_be_printable_lines) {
    CHECK(is_valid_known_hosts("host ssh-ed25519 AAAA\nother ssh-rsa BBBB\n"));
    CHECK(is_valid_known_hosts(""));
    CHECK(!is_valid_known_hosts("host\tkey\n"));
    CHECK(!is_valid_known_hosts("caf\xc3\xa9\n"));
    CHECK(!is_valid_known_hosts(std::string(9000, 'a')));
}

TEST(saving_settings_makes_a_new_generation) {
    Rig r;
    auto reply =
        r.ask({"config-save"}, R"({"schema_version":1,"device":{"keyboard":{"layout":"gb"}}})");
    REQUIRE(reply.ok);
    CHECK_EQ(r.saved().keyboard_layout, "gb");
    CHECK_EQ(r.saved().generation, 1U);
    CHECK(!r.privileged.writable); // read-only again
    reply = r.ask({"config-save"}, R"({"schema_version":1})");
    REQUIRE(reply.ok);
    CHECK_EQ(r.saved().generation, 2U);
}

TEST(invalid_settings_are_not_saved) {
    Rig r;
    const auto reply = r.ask({"config-save"}, R"({"schema_version":2})");
    CHECK(!reply.ok);
    CHECK(!reply.message.empty());
    CHECK_EQ(r.privileged.writes, 0);
    CHECK(!fs::exists(r.state("config.json")));
}

TEST(saving_host_keys_checks_them) {
    Rig r;
    CHECK(r.ask({"known-hosts-save"}, "host ssh-ed25519 AAAA\n").ok);
    CHECK_EQ(read(r.state("known_hosts")), "host ssh-ed25519 AAAA\n");
    CHECK(!r.ask({"known-hosts-save"}, "bad\x01line\n").ok);
    CHECK_EQ(read(r.state("known_hosts")), "host ssh-ed25519 AAAA\n");
}

TEST(reset_removes_settings_keys_and_host_keys) {
    Rig r;
    REQUIRE(r.ask({"config-save"}, R"({"schema_version":1})").ok);
    REQUIRE(r.ask({"known-hosts-save"}, "h k\n").ok);
    REQUIRE(r.ask({"keygen"}).ok);
    REQUIRE(r.ask({"config-reset"}).ok);
    for (const auto* name :
         {"config.json", "config.prev.json", "known_hosts", "id_ed25519", "id_ed25519.pub"})
        CHECK(!fs::exists(r.state(name)));
}

TEST(network_settings_are_written_and_applied) {
    Rig r;
    REQUIRE(r.ask({"network", "static", "10.0.0.2/24", "10.0.0.1", "-", "-"}).ok);
    CHECK(read(r.dir.path() / "network/20-wired.network").contains("Address=10.0.0.2/24"));
    REQUIRE_EQ(r.privileged.ran.size(), 2U);
    CHECK(r.privileged.ran[0] == (std::vector<std::string>{"/usr/bin/networkctl", "reload"}));
    CHECK(r.privileged.ran[1]
          == (std::vector<std::string>{"/usr/bin/networkctl", "reconfigure", "eth0"}));
    CHECK(!r.ask({"network", "static", "nonsense", "-", "-", "-"}).ok);
}

TEST(keygen_makes_the_key_once) {
    Rig r;
    const auto first = r.ask({"keygen"});
    REQUIRE(first.ok);
    CHECK_EQ(first.payload, "ssh-ed25519 AAAA pixterm@test\n");
    CHECK_EQ(read(r.state("id_ed25519")), "PRIVATE");
    REQUIRE_EQ(r.privileged.handed_over.size(), 1U);
    CHECK(r.privileged.handed_over[0] == r.state("id_ed25519"));
    CHECK(!fs::exists(r.dir.path() / "keygen"));
    const auto again = r.ask({"keygen"});
    CHECK(again.ok);
    CHECK_EQ(again.payload, first.payload);
    CHECK_EQ(r.privileged.ran.size(), 1U); // not made twice
}

TEST(export_writes_settings_and_keys_to_the_drive) {
    Rig r;
    r.privileged.usb_source = r.dir.path() / "usb-drive";
    REQUIRE(
        r.ask({"config-save"}, R"({"schema_version":1,"device":{"keyboard":{"layout":"gb"}}})").ok);
    REQUIRE(r.ask({"known-hosts-save"}, "h k\n").ok);
    REQUIRE(r.ask({"keygen"}).ok);
    const auto reply = r.ask({"usb-export"});
    REQUIRE(reply.ok);
    CHECK(reply.payload.contains("sda1"));
    const auto drive = r.dir.path() / "usb-drive";
    CHECK(read(drive / "pixterm-config.json").contains("\"gb\""));
    CHECK_EQ(read(drive / "pixterm-known_hosts"), "h k\n");
    CHECK(fs::exists(drive / "pixterm-terminal-key.pub"));
    CHECK(!fs::exists(drive / "id_ed25519")); // never the private key
}

TEST(import_replaces_the_settings_from_the_drive) {
    Rig r;
    r.privileged.usb_source = r.dir.path() / "usb-drive";
    std::ofstream(r.dir.path() / "usb-drive/pixterm-config.json")
        << R"({"schema_version":1,"device":{"keyboard":{"layout":"fr"}}})";
    std::ofstream(r.dir.path() / "usb-drive/pixterm-known_hosts") << "h k\n";
    const auto reply = r.ask({"usb-import"});
    REQUIRE(reply.ok);
    CHECK_EQ(r.saved().keyboard_layout, "fr");
    CHECK_EQ(read(r.state("known_hosts")), "h k\n");
}

TEST(import_refuses_bad_settings) {
    Rig r;
    r.privileged.usb_source = r.dir.path() / "usb-drive";
    std::ofstream(r.dir.path() / "usb-drive/pixterm-config.json") << "{";
    const auto reply = r.ask({"usb-import"});
    CHECK(!reply.ok);
    CHECK(reply.message.contains("not valid"));
    CHECK(!fs::exists(r.state("config.json")));
}

TEST(no_drive_is_explained) {
    Rig r;
    const auto reply = r.ask({"usb-export"});
    CHECK(!reply.ok);
    CHECK(reply.message.contains("No USB drive"));
}

TEST(unknown_requests_and_wrong_arguments_are_refused) {
    Rig r;
    CHECK(!r.ask({"format-disk"}).ok);
    CHECK(!r.ask({"reboot", "now"}).ok);
    CHECK(!r.ask({"config-reset", "x"}).ok);
}

TEST(reboot_runs_systemctl) {
    Rig r;
    CHECK(r.ask({"reboot"}).ok);
    REQUIRE_EQ(r.privileged.ran.size(), 1U);
    CHECK(r.privileged.ran[0]
          == (std::vector<std::string>{"/usr/bin/systemctl", "--no-block", "reboot"}));
}

TEST(boot_applies_the_saved_network) {
    Rig r;
    REQUIRE(
        r.ask(
             {"config-save"},
             R"({"schema_version":1,"device":{"network":{"method":"static","address":"10.1.1.1/16"}}})")
            .ok);
    CHECK(r.helper.boot(false).has_value());
    CHECK(read(r.dir.path() / "network/20-wired.network").contains("Address=10.1.1.1/16"));
    CHECK(r.helper.boot(true).has_value()); // a factory reset first
    CHECK(read(r.dir.path() / "network/20-wired.network").contains("DHCP=ipv4"));
    CHECK(!fs::exists(r.state("config.json")));
}

// ---- Wi-Fi ----

namespace {

const std::vector<std::string> restart_wifi{"/usr/bin/systemctl", "restart",
                                            "wpa_supplicant@wlan0.service"};
const std::vector<std::string> start_wifi{"/usr/bin/systemctl", "start",
                                          "wpa_supplicant@wlan0.service"};
const std::vector<std::string> stop_wifi{"/usr/bin/systemctl", "stop",
                                         "wpa_supplicant@wlan0.service"};

} // namespace

TEST(joining_wifi_keeps_the_passphrase_for_root_alone) {
    Rig r;
    const auto ssid = to_hex("home");
    REQUIRE(r.ask({"wifi", "on", "GB", ssid, "0"}, "correct horse").ok);
    const auto secret = r.state("wifi-passphrase");
    CHECK_EQ(read(secret), ssid + "\ncorrect horse\n");
    CHECK((fs::status(secret).permissions() & fs::perms::all)
          == (fs::perms::owner_read | fs::perms::owner_write));
    CHECK((fs::status(r.wifi_conf()).permissions() & fs::perms::all)
          == (fs::perms::owner_read | fs::perms::owner_write));
    const auto conf = read(r.wifi_conf());
    CHECK(conf.contains("country=GB\n"));
    CHECK(conf.contains("sae_password=\"correct horse\"\n"));
    CHECK(r.ran(restart_wifi));
}

TEST(wifi_keeps_the_stored_passphrase_for_the_same_network) {
    Rig r;
    REQUIRE(r.ask({"wifi", "on", "GB", to_hex("home"), "0"}, "correct horse").ok);
    REQUIRE(r.ask({"wifi", "on", "FR", to_hex("home"), "1"}).ok);
    const auto conf = read(r.wifi_conf());
    CHECK(conf.contains("country=FR\n"));
    CHECK(conf.contains("scan_ssid=1\n"));
    CHECK(conf.contains("psk=\"correct horse\"\n"));
    const auto other = r.ask({"wifi", "on", "GB", to_hex("cafe"), "0"});
    CHECK(!other.ok);
    CHECK(other.message.contains("passphrase"));
}

TEST(wifi_arguments_are_checked) {
    Rig r;
    const auto ssid = to_hex("home");
    CHECK(!r.ask({"wifi", "on", "gb", ssid, "0"}, "correct horse").ok);
    CHECK(!r.ask({"wifi", "on", "GB", "zz", "0"}, "correct horse").ok);
    CHECK(!r.ask({"wifi", "on", "GB", to_hex("a\nb"), "0"}, "correct horse").ok);
    CHECK(!r.ask({"wifi", "on", "GB", ssid, "2"}, "correct horse").ok);
    CHECK(!r.ask({"wifi", "on", "GB", ssid, "0"}, "short").ok);
    CHECK(!r.ask({"wifi", "on", "GB", ssid, "0"}, "two\nlines here").ok);
    CHECK(!r.ask({"wifi", "on", "GB", ssid}, "correct horse").ok);
    CHECK(!r.ask({"wifi", "sideways"}).ok);
    CHECK(!r.ask({"wifi-scan"}).ok);
    CHECK(!fs::exists(r.state("wifi-passphrase")));
}

TEST(a_terminal_without_wifi_says_so) {
    Rig r;
    r.privileged.wifi.clear();
    const auto reply = r.ask({"wifi", "on", "GB", to_hex("home"), "0"}, "correct horse");
    CHECK(!reply.ok);
    CHECK(reply.message.contains("no Wi-Fi"));
    CHECK(!r.ask({"wifi-scan", "GB"}).ok);
}

TEST(turning_wifi_off_forgets_the_passphrase) {
    Rig r;
    REQUIRE(r.ask({"wifi", "on", "GB", to_hex("home"), "0"}, "correct horse").ok);
    REQUIRE(r.ask({"wifi", "off"}).ok);
    CHECK(!fs::exists(r.state("wifi-passphrase")));
    CHECK(!fs::exists(r.wifi_conf()));
    CHECK(r.privileged.ran.back() == stop_wifi);
}

TEST(a_scan_runs_wpa_supplicant_only_while_it_lasts) {
    Rig r;
    r.privileged.scan_output = "bssid / frequency / signal level / flags / ssid\n"
                               "aa:bb:cc:dd:ee:01\t2437\t-48\t[WPA2-PSK-CCMP][ESS]\thome\n";
    const auto reply = r.ask({"wifi-scan", "GB"});
    REQUIRE(reply.ok);
    CHECK_EQ(reply.payload, "-48 wpa2 686f6d65\n");
    CHECK(r.ran(start_wifi));
    CHECK(r.ran({"/usr/sbin/wpa_cli", "-i", "wlan0", "scan"}));
    CHECK(r.privileged.ran.back() == stop_wifi);
    CHECK(!fs::exists(r.wifi_conf()));
}

TEST(a_scan_while_joined_leaves_wpa_supplicant_running) {
    Rig r;
    REQUIRE(r.ask({"wifi", "on", "GB", to_hex("home"), "0"}, "correct horse").ok);
    REQUIRE(r.ask({"wifi-scan", "GB"}).ok);
    CHECK(!r.ran(stop_wifi));
    CHECK(read(r.wifi_conf()).contains("network="));
}

TEST(reset_forgets_wifi) {
    Rig r;
    REQUIRE(r.ask({"wifi", "on", "GB", to_hex("home"), "0"}, "correct horse").ok);
    REQUIRE(r.ask({"config-reset"}).ok);
    CHECK(!fs::exists(r.state("wifi-passphrase")));
    CHECK(!fs::exists(r.wifi_conf()));
}

TEST(boot_joins_the_saved_wifi_network) {
    Rig r;
    REQUIRE(r.ask({"wifi", "on", "GB", to_hex("home"), "0"}, "correct horse").ok);
    REQUIRE(r.ask({"config-save"}, R"({"schema_version":1,"device":{"wifi":{
                  "enabled":true,"ssid":"home","country":"GB"}}})")
                .ok);
    fs::remove(r.wifi_conf()); // /run is empty at boot
    CHECK(r.helper.boot(false).has_value());
    CHECK(read(r.wifi_conf()).contains("psk=\"correct horse\""));
}

TEST(boot_leaves_wifi_off_without_a_passphrase) {
    Rig r;
    REQUIRE(r.ask({"config-save"}, R"({"schema_version":1,"device":{"wifi":{
                  "enabled":true,"ssid":"home","country":"GB"}}})")
                .ok);
    CHECK(r.helper.boot(false).has_value());
    CHECK(!fs::exists(r.wifi_conf()));
}

// ---- RDP certificates ----

namespace {

constexpr std::string_view pem =
    "-----BEGIN CERTIFICATE-----\nMIIBszCCAVmgAwIBAgIU\nAAAA+/9=\n-----END CERTIFICATE-----\n";

} // namespace

TEST(rdp_certificates_are_checked_then_saved) {
    Rig r;
    REQUIRE(r.ask({"rdp-cert-save", "nt.example.org_3389.pem"}, std::string(pem)).ok);
    CHECK_EQ(read(r.state("rdp-certs/nt.example.org_3389.pem")), pem);
    REQUIRE(r.ask({"rdp-cert-save", "2001:db8::1_3390.pem"}, std::string(pem)).ok);

    for (const auto* name : {"../x_1.pem", "nt.pem", "nt_3389.crt", "-nt_3389.pem", "nt_0.pem",
                             "nt_99999.pem", "nt_33a.pem"})
        CHECK(!r.ask({"rdp-cert-save", name}, std::string(pem)).ok);
    for (const auto& bad :
         {std::string("hello\n"), std::string(pem.substr(0, 40)), std::string(pem) + "rm -rf /\n",
          std::string("-----BEGIN CERTIFICATE-----\nab cd\n-----END CERTIFICATE-----\n"),
          "-----BEGIN CERTIFICATE-----\n" + std::string(20000, 'A')
              + "\n-----END CERTIFICATE-----\n"})
        CHECK(!r.ask({"rdp-cert-save", "nt_3389.pem"}, bad).ok);
    CHECK(!r.ask({"rdp-cert-save"}, std::string(pem)).ok);
}

TEST(reset_forgets_rdp_certificates) {
    Rig r;
    REQUIRE(r.ask({"rdp-cert-save", "nt_3389.pem"}, std::string(pem)).ok);
    REQUIRE(r.ask({"config-reset"}).ok);
    CHECK(!fs::exists(r.state("rdp-certs/nt_3389.pem")));
}

// ---- forgetting a host's key ----

TEST(known_hosts_lose_the_lines_for_one_host) {
    const std::string text = "nt ssh-ed25519 AAAA\n"
                             "[nt]:2222 ssh-ed25519 BBBB\n"
                             "other,nt ssh-rsa CCCC\n"
                             "ntx ssh-ed25519 DDDD\n"
                             "[ntx]:22 ssh-ed25519 EEEE\n";
    CHECK_EQ(known_hosts_without(text, "nt", 22),
             "[nt]:2222 ssh-ed25519 BBBB\nntx ssh-ed25519 DDDD\n[ntx]:22 ssh-ed25519 EEEE\n");
    CHECK_EQ(known_hosts_without(text, "nt", 2222),
             "nt ssh-ed25519 AAAA\nother,nt ssh-rsa CCCC\nntx ssh-ed25519 DDDD\n"
             "[ntx]:22 ssh-ed25519 EEEE\n");
}

TEST(forgetting_a_host_key_removes_its_ssh_key_and_rdp_certificate) {
    Rig r;
    REQUIRE(r.ask({"known-hosts-save"}, "nt ssh-ed25519 AAAA\nvms ssh-ed25519 BBBB\n").ok);
    REQUIRE(r.ask({"rdp-cert-save", "nt_3389.pem"}, std::string(pem)).ok);
    REQUIRE(r.ask({"host-key-forget", "nt", "22"}).ok);
    CHECK_EQ(read(r.state("known_hosts")), "vms ssh-ed25519 BBBB\n");
    REQUIRE(r.ask({"host-key-forget", "nt", "3389"}).ok);
    CHECK(!fs::exists(r.state("rdp-certs/nt_3389.pem")));
    REQUIRE(r.ask({"host-key-forget", "unknown", "22"}).ok); // nothing to forget
    CHECK(!r.ask({"host-key-forget", "-x", "22"}).ok);
    CHECK(!r.ask({"host-key-forget", "nt", "0"}).ok);
    CHECK(!r.ask({"host-key-forget", "nt"}).ok);
}

// ---- diagnostics ----

TEST(diagnostics_go_to_the_drive_without_the_pin) {
    Rig r;
    r.privileged.usb_source = r.dir.path() / "usb-drive";
    std::ofstream(r.dir.path() / "Xorg.log") << "(EE) no screens found\n";
    REQUIRE(r.ask({"config-save"}, R"({"schema_version":1,"device":{"admin_pin":")"
                                       + make_pin_hash("1234").value_or("") + R"("}})")
                .ok);
    const auto reply = r.ask({"usb-diagnostics"});
    REQUIRE(reply.ok);
    CHECK(reply.payload.contains("sda1"));
    const auto dir = r.dir.path() / "usb-drive/pixterm-diagnostics";
    CHECK(read(dir / "Xorg.log").contains("no screens found"));
    CHECK(fs::exists(dir / "journal.txt"));
    CHECK(r.ran({"/usr/bin/journalctl", "-b", "--no-pager"}));
    CHECK(!read(dir / "settings.json").contains("admin_pin"));
    CHECK(read(dir / "settings.json").contains("schema_version"));
}
