// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// LinuxSystem against real processes, pipes and sockets.

#include "temp_dir.hpp"
#include "test.hpp"

#include <pixterm/fd.hpp>

#include <linux/event_loop.hpp>
#include <linux/linux_system.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <optional>

using namespace pixterm;
using namespace pixterm::supervisor;
using pixterm::test::TempDir;

namespace {

struct Recorder final : Events {
    bool ready = false;
    std::optional<std::tuple<Slot, ExitStatus, std::string>> exit;
    std::vector<std::string> lines;
    std::vector<std::string> console_lines;

    void x_ready() override { ready = true; }
    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        exit = {slot, status, std::string(output)};
    }
    void chooser_line(std::string_view line) override { lines.emplace_back(line); }
    void console_line(std::string_view line) override { console_lines.emplace_back(line); }
};

struct Rig {
    TempDir dir;
    EventLoop loop{SIGCHLD};
    std::unique_ptr<LinuxSystem> sys;
    Recorder events;

    Rig() {
        sys = std::make_unique<LinuxSystem>(
            loop, LinuxPaths{.xorg = "/bin/false",
                             .xauth = "/bin/true",
                             .xauthority = dir.path() / "Xauthority",
                             .xorg_log = dir.path() / "Xorg.log",
                             .xorg_conf = dir.path() / "xorg.conf",
                             .log_file = dir.path() / "pixtermd.log",
                             .helper_socket = dir.path() / "helper.sock",
                             .state_dir = dir.path(),
                             .factory_settings = dir.path() / "factory.json",
                             .env = {"PATH=/usr/bin:/bin"}});
        sys->set_events(events);
        loop.on_signal = [this](int) { sys->reap(); };
    }

    // Runs the loop until done() or five seconds pass.
    template <typename Done>
    bool run_until(Done done) {
        const auto limit = Clock::now() + std::chrono::seconds(5);
        while (!done() && Clock::now() < limit) {
            loop.run_once(Clock::now() + std::chrono::milliseconds(50));
            sys->tick();
        }
        return done();
    }

    static Program shell(std::string script) {
        return {.name = "sh",
                .argv = {"/bin/sh", "-c", std::move(script)},
                .env = {"PATH=/usr/bin:/bin"},
                .input = {},
                .hold_input = false};
    }
};

} // namespace

TEST(a_program_exit_comes_with_its_output) {
    Rig r;
    REQUIRE(r.sys->start(Slot::client, Rig::shell("echo out; echo err >&2; exit 3")));
    CHECK(r.sys->running(Slot::client));
    REQUIRE(r.run_until([&] { return r.events.exit.has_value(); }));
    REQUIRE(r.events.exit.has_value());
    const auto& [slot, status, output] = *r.events.exit;
    CHECK(slot == Slot::client);
    CHECK_EQ(status.code, 3);
    CHECK(output.contains("out"));
    CHECK(output.contains("err"));
    CHECK(!r.sys->running(Slot::client));
}

TEST(input_reaches_the_program_and_can_stay_open) {
    Rig r;
    auto p = Rig::shell("read x; echo got $x; read y || echo closed");
    p.input = "pw\n";
    p.hold_input = true;
    REQUIRE(r.sys->start(Slot::client, std::move(p)));
    // The program waits on its second read until its input is closed.
    r.run_until([] { return false; });
    CHECK(!r.events.exit.has_value());
    r.sys->close_input(Slot::client);
    REQUIRE(r.run_until([&] { return r.events.exit.has_value(); }));
    REQUIRE(r.events.exit.has_value());
    const auto& output = std::get<2>(*r.events.exit);
    CHECK(output.contains("got pw"));
    CHECK(output.contains("closed"));
}

TEST(signals_reach_the_whole_process_group) {
    Rig r;
    REQUIRE(r.sys->start(Slot::client, Rig::shell("sleep 30 & wait")));
    r.sys->signal(Slot::client, SIGTERM);
    REQUIRE(r.run_until([&] { return r.events.exit.has_value(); }));
    REQUIRE(r.events.exit.has_value());
    CHECK_EQ(std::get<1>(*r.events.exit).signal, SIGTERM);
}

TEST(a_job_hands_its_output_to_its_callback) {
    Rig r;
    std::optional<std::pair<ExitStatus, std::string>> result;
    REQUIRE(r.sys->start_job(
        {.name = "echo",
         .task = Rig::shell("echo hello"),
         .timeout = std::chrono::seconds(5),
         .secret = false},
        [&](ExitStatus s, std::string_view out) { result = {s, std::string(out)}; }));
    CHECK(r.sys->job_running());
    CHECK(!r.sys->start_job({.name = "second",
                             .task = Rig::shell("true"),
                             .timeout = std::chrono::seconds(5),
                             .secret = false},
                            [](ExitStatus, std::string_view) {}));
    REQUIRE(r.run_until([&] { return result.has_value(); }));
    REQUIRE(result.has_value());
    CHECK(result->first.succeeded());
    CHECK_EQ(result->second, "hello");
    CHECK(!r.sys->job_running());
}

TEST(a_job_that_takes_too_long_is_killed) {
    Rig r;
    std::optional<ExitStatus> result;
    REQUIRE(r.sys->start_job({.name = "sleep",
                              .task = Rig::shell("sleep 30"),
                              .timeout = std::chrono::milliseconds(100),
                              .secret = false},
                             [&](ExitStatus s, std::string_view) { result = s; }));
    REQUIRE(r.run_until([&] { return result.has_value(); }));
    REQUIRE(result.has_value());
    CHECK_EQ(result->signal, SIGKILL);
}

TEST(a_cancelled_job_never_calls_back) {
    Rig r;
    bool called = false;
    REQUIRE(r.sys->start_job({.name = "sleep",
                              .task = Rig::shell("sleep 30"),
                              .timeout = std::chrono::seconds(5),
                              .secret = false},
                             [&](ExitStatus, std::string_view) { called = true; }));
    r.sys->cancel_job();
    REQUIRE(r.run_until([&] { return !r.sys->job_running(); }));
    CHECK(!called);
}

TEST(the_tcp_probe_finds_a_listener) {
    Rig r;
    const UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t length = sizeof address;
    REQUIRE(::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    REQUIRE(::listen(listener.get(), 1) == 0);
    REQUIRE(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) == 0);
    const int port = ntohs(address.sin_port);

    std::optional<std::string> result;
    const auto done = [&](ExitStatus, std::string_view out) { result = std::string(out); };
    REQUIRE(
        r.sys->start_job({.name = "tcp-probe",
                          .task = TcpProbe{.address = "127.0.0.1", .port = port, .wake_mac = {}},
                          .timeout = std::chrono::seconds(5),
                          .secret = false},
                         done));
    REQUIRE(r.run_until([&] { return result.has_value(); }));
    REQUIRE(result.has_value());
    CHECK_EQ(*result, "OK");
}

TEST(the_tcp_probe_explains_a_refusal) {
    Rig r;
    std::optional<std::string> result;
    REQUIRE(r.sys->start_job({.name = "tcp-probe",
                              .task = TcpProbe{.address = "127.0.0.1", .port = 1, .wake_mac = {}},
                              .timeout = std::chrono::seconds(5),
                              .secret = false},
                             [&](ExitStatus, std::string_view out) { result = std::string(out); }));
    REQUIRE(r.run_until([&] { return result.has_value(); }));
    REQUIRE(result.has_value());
    CHECK(result->starts_with("E "));
}

TEST(the_chooser_talks_over_its_socket) {
    Rig r;
    REQUIRE(r.sys->start_chooser(
        Rig::shell("echo HELLO >&$PIXTERM_CONTROL_FD; read reply <&$PIXTERM_CONTROL_FD; "
                   "echo \"$reply\"")));
    REQUIRE(r.run_until([&] { return !r.events.lines.empty(); }));
    CHECK_EQ(r.events.lines.front(), "HELLO");
    CHECK(r.sys->chooser_connected());
    r.sys->send_to_chooser("IDLE");
    REQUIRE(r.run_until([&] { return r.events.exit.has_value(); }));
    REQUIRE(r.events.exit.has_value());
    CHECK_EQ(std::get<2>(*r.events.exit), "IDLE");
    CHECK(!r.sys->chooser_connected());
}

TEST(helper_requests_go_over_its_socket) {
    Rig r;
    const UniqueFd server(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    (r.dir.path() / "helper.sock").string().copy(address.sun_path, sizeof address.sun_path - 1);
    REQUIRE(::bind(server.get(), reinterpret_cast<sockaddr*>(&address), sizeof address) == 0);
    REQUIRE(::listen(server.get(), 1) == 0);
    // A stand-in helper on the same loop: it reads the request to its
    // end, then answers OK.
    std::string request;
    UniqueFd client;
    int client_watch = -1;
    r.loop.watch(server.get(), POLLIN, [&](short) {
        client.reset(::accept4(server.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
        client_watch = r.loop.watch(client.get(), POLLIN, [&](short) {
            std::array<char, 512> buffer{};
            const auto n = ::read(client.get(), buffer.data(), buffer.size());
            if (n > 0) {
                request.append(buffer.data(), static_cast<std::size_t>(n));
                return;
            }
            (void)write_all(client.get(), helper::format_ok("done"));
            r.loop.unwatch(client_watch);
            client.reset();
        });
    });

    std::optional<helper::Reply> reply;
    REQUIRE(r.sys->call_helper({.args = {"keygen"}, .payload = {}},
                               [&](const helper::Reply& rep) { reply = rep; }));
    CHECK(r.sys->helper_busy());
    REQUIRE(r.run_until([&] { return reply.has_value(); }));
    REQUIRE(reply.has_value());
    CHECK_EQ(request, "0 keygen\n");
    CHECK(reply->ok);
    CHECK_EQ(reply->payload, "done");
    CHECK(!r.sys->helper_busy());
}

TEST(a_missing_helper_is_reported) {
    Rig r;
    std::optional<helper::Reply> reply;
    REQUIRE(r.sys->call_helper({.args = {"keygen"}, .payload = {}},
                               [&](const helper::Reply& rep) { reply = rep; }));
    REQUIRE(reply.has_value());
    CHECK(!reply->ok);
    CHECK(reply->message.contains("not available"));
}

TEST(files_are_read_and_written) {
    Rig r;
    const auto path = r.dir.path() / "f";
    CHECK(!r.sys->read_file(path));
    REQUIRE(r.sys->write_file(path, "text"));
    CHECK_EQ(r.sys->read_file(path).value_or(""), "text");
    CHECK(!r.sys->can_open(path));
    CHECK(!r.sys->is_socket(path));
    r.sys->remove_file(path);
    CHECK(r.sys->can_open(path) == std::errc::no_such_file_or_directory);
}

TEST(xorg_reads_the_keyboard_and_display_size_from_the_settings) {
    Rig r;
    Settings s;
    s.keyboard_layout = "de";
    s.keyboard_model = "sun_type6_usb";
    s.display_mode = "1280x1024@75";
    CHECK(r.sys->start_xorg(s, {}));
    const auto conf = r.sys->read_file(r.dir.path() / "xorg.conf").value_or("");
    CHECK(conf.contains(R"(Option "XkbLayout" "de")"));
    CHECK(conf.contains(R"(Option "XkbModel" "sun_type6_usb")"));
    // Xorg starts every display, XDMCP ones too, at the chosen size.
    CHECK(conf.contains(R"(Identifier "HDMI-1")"));
    CHECK(conf.contains(R"(Identifier "HDMI-2")"));
    CHECK(conf.contains(R"(Option "PreferredMode" "1280x1024")"));
}

TEST(xorg_picks_the_size_itself_when_the_mode_is_automatic) {
    Rig r;
    CHECK(r.sys->start_xorg(Settings{}, {}));
    const auto conf = r.sys->read_file(r.dir.path() / "xorg.conf").value_or("");
    CHECK(conf.contains("XkbLayout"));
    CHECK(!conf.contains("Monitor"));
}

TEST(the_console_shows_text_and_reads_typed_lines) {
    Rig r;
    std::array<int, 2> fds{};
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds.data()) == 0);
    const UniqueFd ours(fds[0]);
    const UniqueFd tty(fds[1]);
    r.sys->use_console(tty.get());

    r.sys->show_console("Hello");
    std::array<char, 64> shown{};
    const auto n = ::read(ours.get(), shown.data(), shown.size());
    REQUIRE(n > 0);
    CHECK_EQ(std::string_view(shown.data(), static_cast<std::size_t>(n)), "\033[H\033[2JHello");
    r.sys->console_echo(false); // not a tty: ignored

    const std::string_view typed = "rese";
    REQUIRE(::write(ours.get(), typed.data(), typed.size()) == 4);
    REQUIRE(::write(ours.get(), "t\r\n\n", 4) == 4);
    REQUIRE(r.run_until([&] { return r.events.console_lines.size() == 2; }));
    CHECK(r.events.console_lines == (std::vector<std::string>{"reset", ""}));
}
