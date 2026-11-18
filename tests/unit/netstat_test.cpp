// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "temp_dir.hpp"
#include "test.hpp"

#include <pixterm/netstat.hpp>

#include <filesystem>
#include <fstream>

using namespace pixterm;
using pixterm::test::TempDir;
namespace fs = std::filesystem;

namespace {

// Two established connections to port 6000 (0x1770), one listening
// socket on it, and one established connection to port 22.
constexpr std::string_view proc_net_tcp =
    "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid\n"
    "   0: 00000000:1770 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000\n"
    "   1: 0A21280A:1770 1028210A:C350 01 00000000:00000000 00:00000000 00000000  1000\n"
    "   2: 0A21280A:1770 1128210A:C351 01 00000000:00000000 00:00000000 00000000  1000\n"
    "   3: 0A21280A:0016 1028210A:C352 01 00000000:00000000 00:00000000 00000000     0\n"
    "garbage\n";

constexpr std::string_view proc_net_unix =
    "Num       RefCount Protocol Flags    Type St Inode Path\n"
    "0000000000000000: 00000002 00000000 00010000 0001 01 1234 /tmp/.X11-unix/X0\n"
    "0000000000000000: 00000003 00000000 00000000 0001 03 1235 /tmp/.X11-unix/X0\n"
    "0000000000000000: 00000003 00000000 00000000 0001 03 1236 @/tmp/.X11-unix/X0\n"
    "0000000000000000: 00000003 00000000 00000000 0001 03 1237 /tmp/.X11-unix/X1\n"
    "0000000000000000: 00000003 00000000 00000000 0001 03 1238\n";

void make_interface(const fs::path& net, std::string_view name, std::string_view type,
                    std::string_view carrier, bool physical, bool wireless) {
    const auto dir = net / name;
    fs::create_directories(dir);
    std::ofstream(dir / "type") << type << '\n';
    std::ofstream(dir / "carrier") << carrier << '\n';
    if (physical)
        fs::create_directory(dir / "device");
    if (wireless)
        fs::create_directory(dir / "wireless");
}

} // namespace

TEST(counts_established_connections_to_a_port) {
    CHECK_EQ(count_established(proc_net_tcp, 6000), 2);
    CHECK_EQ(count_established(proc_net_tcp, 22), 1);
    CHECK_EQ(count_established(proc_net_tcp, 6001), 0);
    CHECK_EQ(count_established("", 6000), 0);
}

TEST(counts_connected_x_sockets) {
    CHECK_EQ(count_connected_x_sockets(proc_net_unix, 0), 2);
    CHECK_EQ(count_connected_x_sockets(proc_net_unix, 1), 1);
    CHECK_EQ(count_connected_x_sockets(proc_net_unix, 2), 0);
}

TEST(carrier_comes_from_physical_wired_and_wireless_interfaces) {
    const TempDir dir;
    CHECK(!link_carrier(dir.path() / "missing"));
    make_interface(dir.path(), "lo", "772", "1", false, false);
    make_interface(dir.path(), "br0", "1", "1", false, false);
    CHECK(!link_carrier(dir.path())); // no physical interface
    make_interface(dir.path(), "eth0", "1", "0", true, false);
    make_interface(dir.path(), "wlan0", "1", "0", true, true);
    CHECK(link_carrier(dir.path()) == false);
    // Joined to a Wi-Fi network, with the cable out.
    make_interface(dir.path(), "wlan0", "1", "1", true, true);
    CHECK(link_carrier(dir.path()) == true);
    make_interface(dir.path(), "wlan0", "1", "0", true, true);
    make_interface(dir.path(), "eth1", "1", "1", true, false);
    CHECK(link_carrier(dir.path()) == true);
}

TEST(display_connections_read_proc) {
    const TempDir dir;
    CHECK(!display_connections(0, dir.path()));
    fs::create_directories(dir.path() / "net");
    std::ofstream(dir.path() / "net/tcp") << proc_net_tcp;
    std::ofstream(dir.path() / "net/unix") << proc_net_unix;
    CHECK(display_connections(0, dir.path()) == 4);
    CHECK(display_tcp_connections(0, dir.path()) == 2);
}
