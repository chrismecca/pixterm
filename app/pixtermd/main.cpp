// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// pixtermd, the supervisor: starts and watches Xorg, mwm, the chooser
// and every session. systemd starts it in a logind session on the first
// virtual console, so Xorg runs without root. It links no X library,
// survives Xorg crashes, and never blocks: short programs run as jobs,
// and privileged requests go to pixterm-helper over its socket.

#include <pixterm/build_config.hpp>

#include <linux/event_loop.hpp>
#include <linux/linux_system.hpp>
#include <pwd.h>
#include <supervisor.hpp>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <print>
#include <string>

using namespace pixterm;
using namespace pixterm::supervisor;

namespace {

std::string from_environment(const char* name, std::string_view fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

} // namespace

int main() {
    ::umask(077);
    (void)std::signal(SIGPIPE, SIG_IGN);
    (void)std::setvbuf(stderr, nullptr, _IOLBF, 0);

    // systemd gives the runtime directory; it is emptied at every boot.
    const std::filesystem::path runtime = from_environment("RUNTIME_DIRECTORY", "");
    if (runtime.empty()) {
        std::println(stderr, "pixtermd: RUNTIME_DIRECTORY is not set; start pixtermd from its "
                             "systemd unit");
        return 1;
    }
    const std::filesystem::path state = from_environment("STATE_DIRECTORY", build::state_dir);
    const auto factory = from_environment("PIXTERM_CONFIG", build::factory_settings);
    // HOME, with the directory FreeRDP keeps host certificates in.
    const auto home = runtime / "home";
    for (const auto& dir :
         {home, home / ".config", home / ".config/freerdp", home / ".config/freerdp/server"}) {
        if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
            std::println(stderr, "pixtermd: cannot make {}: {}", dir.string(),
                         std::strerror(errno));
            return 1;
        }
    }
    const passwd* user = ::getpwuid(::getuid());
    const std::string name = user != nullptr ? user->pw_name : "pixterm";

    const std::vector<std::string> env{
        "PATH=/usr/bin:/bin",
        "LANG=C.UTF-8",
        "DISPLAY=:0",
        "HOME=" + home.string(),
        "USER=" + name,
        "LOGNAME=" + name,
        "XAUTHORITY=" + (runtime / "Xauthority").string(),
        "XDG_RUNTIME_DIR=" + from_environment("XDG_RUNTIME_DIR", runtime.string()),
        "PIXTERM_CONFIG=" + factory,
        "PIXTERM_STATE_DIR=" + state.string(),
        "PIXTERM_RUNTIME_DIR=" + runtime.string(),
    };

    EventLoop loop({SIGCHLD, SIGTERM, SIGINT, SIGHUP});
    LinuxSystem system(
        loop, {.xorg = std::string(build::xorg),
               .xauth = "/usr/bin/xauth",
               .xauthority = runtime / "Xauthority",
               .xorg_log = runtime / "Xorg.log",
               .xorg_conf = runtime / "xorg-settings.conf",
               .log_file = runtime / "pixtermd.log",
               .helper_socket = from_environment("PIXTERM_HELPER_SOCKET", helper::socket_path),
               .state_dir = state,
               .factory_settings = factory,
               .env = env});
    Supervisor supervisor(system, {.runtime_dir = runtime,
                                   .state_dir = state,
                                   .libexec_dir = std::string(build::libexec_dir),
                                   .data_dir = std::string(build::data_dir),
                                   .base_env = env});
    system.set_events(supervisor);
    // systemd gives pixtermd tty1 as its input; it is the text console
    // that explains an X server that cannot start.
    if (::isatty(STDIN_FILENO) == 1)
        system.use_console(STDIN_FILENO);

    bool stopping = false;
    loop.on_signal = [&](int signal) {
        if (signal == SIGCHLD)
            system.reap();
        else
            stopping = true;
    };

    system.log("starting");
    supervisor.start();
    while (!stopping) {
        auto deadline = supervisor.next_deadline();
        const auto other = system.next_deadline();
        if (other && (!deadline || *other < *deadline))
            deadline = other;
        loop.run_once(deadline);
        system.tick();
        supervisor.tick();
    }
    system.log("stopping");
    system.terminate_all();
    return 0;
}
