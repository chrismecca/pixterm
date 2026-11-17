// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "temp_dir.hpp"
#include "test.hpp"

#include <pixterm/settings_store.hpp>

#include <helper.hpp>

#include <filesystem>
#include <fstream>

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
            .keygen_dir = dir / "keygen"};
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
