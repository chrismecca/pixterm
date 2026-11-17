// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// pixterm-helper: the only part of pixterm that runs as root.
//
// systemd starts one for each connection to a socket only the pixterm
// account can open (pixterm-helper.socket, Accept=yes), and the helper
// checks the caller's account again.
//
//     pixterm-helper --inetd                  serve the connection on
//                                             standard input
//     pixterm-helper --listen PATH [--owner USER]
//                                             serve connections to PATH,
//                                             one at a time (tests)
//     pixterm-helper boot-init                at boot: a factory reset if
//                                             asked for, then the network

#include <pixterm/build_config.hpp>
#include <pixterm/fd.hpp>

#include <helper.hpp>
#include <linux_privileged.hpp>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <print>
#include <span>
#include <string_view>

using namespace pixterm;
using namespace pixterm::helper;

namespace {

constexpr std::string_view service_user = "pixterm";
// Anyone with the SD card can make this file on its boot partition to
// ask for a factory reset, for example after forgetting the PIN.
constexpr std::string_view reset_marker = "/boot/firmware/pixterm-reset";

std::string from_environment(const char* name, std::string_view fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

uid_t uid_of(std::string_view user) {
    const passwd* pw = ::getpwnam(std::string(user).c_str());
    return pw != nullptr ? pw->pw_uid : static_cast<uid_t>(-1);
}

int usage() {
    std::println(stderr, "usage: pixterm-helper --inetd\n"
                         "       pixterm-helper --listen PATH [--owner USER]\n"
                         "       pixterm-helper boot-init");
    return 2;
}

int listen_on(const std::string& path, std::string_view owner, Helper& helper, uid_t service) {
    const UniqueFd s(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (!s || path.size() >= sizeof address.sun_path) {
        std::println(stderr, "pixterm-helper: cannot use {}", path);
        return 1;
    }
    path.copy(address.sun_path, path.size());
    ::unlink(path.c_str());
    const passwd* pw = ::getpwnam(std::string(owner).c_str());
    if (::bind(s.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0
        || ::chmod(path.c_str(), 0600) != 0
        || (pw != nullptr && ::chown(path.c_str(), pw->pw_uid, pw->pw_gid) != 0)
        || ::listen(s.get(), 4) != 0) {
        std::println(stderr, "pixterm-helper: {}: {}", path, last_error().message());
        return 1;
    }
    for (;;) {
        const UniqueFd client(::accept4(s.get(), nullptr, nullptr, SOCK_CLOEXEC));
        if (!client) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            return 1;
        }
        serve(client.get(), helper, service);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::span args(argv + 1, static_cast<std::size_t>(argc - 1));
    if (::geteuid() != 0) {
        std::println(stderr, "pixterm-helper: must run as root");
        return 1;
    }
    ::umask(022);
    (void)std::signal(SIGPIPE, SIG_IGN);

    const Paths paths{
        .state_dir = from_environment("PIXTERM_STATE_DIR", build::state_dir),
        .factory_settings = from_environment("PIXTERM_CONFIG", build::factory_settings),
        .network_dir = from_environment("PIXTERM_NETWORK_DIR", "/run/systemd/network"),
        .usb_dir = "/run/pixterm-usb",
        .keygen_dir = "/run/pixterm-keygen",
    };
    const uid_t service = uid_of(service_user);
    LinuxPrivileged privileged(paths, service);
    Helper helper(paths, privileged);

    const auto arg = [&args](std::size_t i) {
        return i < args.size() ? std::string_view(args[i]) : std::string_view();
    };
    if (args.size() == 1 && arg(0) == "--inetd") {
        serve(STDIN_FILENO, helper, service);
        return 0;
    }
    if ((args.size() == 2 || args.size() == 4) && arg(0) == "--listen") {
        if (args.size() == 4 && arg(2) != "--owner")
            return usage();
        return listen_on(std::string(arg(1)), args.size() == 4 ? arg(3) : service_user, helper,
                         service);
    }
    if (args.size() == 1 && arg(0) == "boot-init") {
        const bool reset = ::access(std::string(reset_marker).c_str(), F_OK) == 0;
        if (reset) {
            std::println(stderr, "pixterm-helper: {} found: back to the factory settings",
                         reset_marker);
            // Removed first, so a reset that fails is not tried at every boot.
            if (auto removed = remove_from_read_only(reset_marker); !removed)
                std::println(stderr, "pixterm-helper: {}", removed.error());
        }
        if (auto ok = helper.boot(reset); !ok) {
            std::println(stderr, "pixterm-helper: {}", ok.error());
            return 1;
        }
        return 0;
    }
    return usage();
}
