// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// pixterm-xsetup: readies a freshly started X server.
//
//     pixterm-xsetup
//         Sets the saved display mode, blanking time and keyboard.
//         "auto" leaves the mode Xorg chose from the monitor's EDID.
//     pixterm-xsetup --allow-host HOST
//         The same, then lets HOST open windows over TCP (direct X): adds
//         its addresses to the server's access list and prints
//         "DISPLAY address:0", the name the host should use. Exits with 3
//         if the host could not be let in.
//     pixterm-xsetup --font-server tcp/HOST:PORT
//         Adds a host's font server to the font path (SSH sessions).
//     pixterm-xsetup --full-screen
//         Readies the screen for one program that fills it: the root
//         window turns plain white, so the edge a character grid cannot
//         fill matches a terminal, and the whole screen becomes the work
//         area (with no window manager, toolkits such as FLTK otherwise
//         guess one and shrink their windows). Prints "WIDTH HEIGHT".
//
// pixtermd runs it with DISPLAY and XAUTHORITY set.

#include <pixterm/build_config.hpp>
#include <pixterm/fd.hpp>
#include <pixterm/settings_store.hpp>
#include <pixterm/spawn.hpp>

#include <display.hpp>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <print>
#include <span>
#include <string>

using namespace pixterm;

namespace {

constexpr int exit_failed = 1;
constexpr int exit_usage = 2;
constexpr int exit_not_allowed = 3;

std::string from_environment(const char* name, std::string_view fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

Settings load_settings() {
    auto loaded = SettingsStore(from_environment("PIXTERM_STATE_DIR", build::state_dir),
                                from_environment("PIXTERM_CONFIG", build::factory_settings))
                      .load();
    if (!loaded.problem.empty())
        std::println(stderr, "pixterm-xsetup: settings: {}", loaded.problem);
    return std::move(loaded.settings);
}

bool apply_keyboard(const Settings& s) {
    const int status =
        run_and_wait({"/usr/bin/setxkbmap", "-layout", s.keyboard_layout, "-variant",
                      s.keyboard_variant, "-option", "", "-option", "terminate:ctrl_alt_bksp"},
                     {"PATH=/usr/bin:/bin", "DISPLAY=" + from_environment("DISPLAY", ":0"),
                      "XAUTHORITY=" + from_environment("XAUTHORITY", "")});
    if (status != 0)
        std::println(stderr, "pixterm-xsetup: setxkbmap failed");
    return status == 0;
}

bool apply_display_mode(Display* d, const Settings& s) {
    if (s.display_mode == "auto")
        return true;
    auto monitor = x11::query_monitor(d);
    if (!monitor) {
        std::println(stderr, "pixterm-xsetup: no monitor is connected");
        return false;
    }
    const auto index = find_mode(*monitor, s.display_mode);
    if (!index) {
        // The monitor changed: keep Xorg's choice rather than guess.
        std::println(stderr, "pixterm-xsetup: {} does not offer {}; using automatic",
                     monitor->output, s.display_mode);
        return true;
    }
    return index == monitor->current || x11::set_mode(d, *monitor, *index);
}

// What Xorg's -s option set at start (see Supervisor::start_x), again,
// for a time changed while the server runs.
void apply_blanking(Display* d, const Settings& s) {
    const int seconds = s.blank_minutes * 60;
    XSetScreenSaver(d, seconds, 0, DefaultBlanking, DefaultExposures);
    int event_base = 0;
    int error_base = 0;
    if (DPMSQueryExtension(d, &event_base, &error_base) == False)
        return;
    if (seconds > 0) {
        const auto timeout = static_cast<CARD16>(seconds);
        DPMSSetTimeouts(d, timeout, timeout, timeout);
        DPMSEnable(d);
    } else {
        DPMSDisable(d);
    }
}

struct FreeAddrinfo {
    void operator()(addrinfo* a) const noexcept { freeaddrinfo(a); }
};

// Lets every address of host connect, and prints the terminal's own
// address on the route to it.
bool allow_host(Display* d, const std::string& host) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), "6000", &hints, &found) != 0) {
        std::println(stderr, "pixterm-xsetup: the address of {} could not be found", host);
        return false;
    }
    const std::unique_ptr<addrinfo, FreeAddrinfo> addresses(found);
    int added = 0;
    std::string self;
    for (const auto* a = found; a != nullptr; a = a->ai_next) {
        XHostAddress xa{};
        if (a->ai_family == AF_INET) {
            auto* in = reinterpret_cast<sockaddr_in*>(a->ai_addr);
            xa.family = FamilyInternet;
            xa.length = sizeof in->sin_addr;
            xa.address = reinterpret_cast<char*>(&in->sin_addr);
        } else if (a->ai_family == AF_INET6) {
            auto* in6 = reinterpret_cast<sockaddr_in6*>(a->ai_addr);
            xa.family = FamilyInternet6;
            xa.length = sizeof in6->sin6_addr;
            xa.address = reinterpret_cast<char*>(&in6->sin6_addr);
        } else {
            continue;
        }
        XAddHost(d, &xa);
        ++added;
        if (self.empty()) {
            // Connecting a UDP socket sends nothing; it only picks a route.
            const UniqueFd s(::socket(a->ai_family, SOCK_DGRAM | SOCK_CLOEXEC, 0));
            sockaddr_storage local{};
            socklen_t length = sizeof local;
            std::array<char, NI_MAXHOST> text{};
            if (s && ::connect(s.get(), a->ai_addr, a->ai_addrlen) == 0
                && ::getsockname(s.get(), reinterpret_cast<sockaddr*>(&local), &length) == 0
                && getnameinfo(reinterpret_cast<sockaddr*>(&local), length, text.data(),
                               text.size(), nullptr, 0, NI_NUMERICHOST)
                       == 0)
                self = text.data();
        }
    }
    XSync(d, False);
    if (added == 0)
        return false;
    if (!self.empty())
        std::println("DISPLAY {}:0", self);
    return true;
}

int prepare_screen(Display* d) {
    const int screen = DefaultScreen(d);
    const Window root = RootWindow(d, screen);
    const int width = DisplayWidth(d, screen);
    const int height = DisplayHeight(d, screen);
    const std::array<long, 4> area{0, 0, width, height};
    XChangeProperty(d, root, XInternAtom(d, "_NET_WORKAREA", False), XA_CARDINAL, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char*>(area.data()), 4);
    XSetWindowBackground(d, root, WhitePixel(d, screen));
    XClearWindow(d, root);
    XSync(d, False);
    std::println("{} {}", width, height);
    return 0;
}

// Xlib's error handler has no room for a pointer of ours.
bool& font_error() {
    static bool seen = false;
    return seen;
}

// Adds a font server to the end of the font path. An unreachable server
// is reported, and the session goes on without it.
int add_font_server(Display* d, const std::string& server) {
    int count = 0;
    char** current = XGetFontPath(d, &count);
    std::vector<char*> path(current, current + count);
    int status = 0;
    if (std::ranges::find_if(path, [&](const char* p) { return server == p; }) == path.end()) {
        std::string copy = server;
        path.push_back(copy.data());
        XSetErrorHandler([](Display*, XErrorEvent*) {
            font_error() = true;
            return 0;
        });
        XSetFontPath(d, path.data(), static_cast<int>(path.size()));
        XSync(d, False);
        if (font_error()) {
            std::println(stderr, "pixterm-xsetup: the font server {} cannot be reached", server);
            status = exit_failed;
        }
    }
    XFreeFontPath(current);
    return status;
}

int usage() {
    std::println(stderr, "usage: pixterm-xsetup [--allow-host HOST | --font-server tcp/HOST:PORT "
                         "| --full-screen]");
    return exit_usage;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv + 1, static_cast<std::size_t>(argc - 1));
    const auto arg = [&args](std::size_t i) {
        return i < args.size() ? std::string_view(args[i]) : std::string_view();
    };
    if (args.size() > 2 || (args.size() == 1 && arg(0) != "--full-screen")
        || (args.size() == 2 && arg(0) != "--allow-host" && arg(0) != "--font-server"))
        return usage();
    if (arg(0) == "--font-server" && !is_valid_font_server(arg(1)))
        return usage();
    if (arg(0) == "--allow-host" && !is_valid_address(arg(1)))
        return usage();

    const auto display = x11::open_display();
    if (!display) {
        std::println(stderr, "pixterm-xsetup: cannot open the display");
        return exit_failed;
    }
    if (arg(0) == "--full-screen")
        return prepare_screen(display.get());
    if (arg(0) == "--font-server")
        return add_font_server(display.get(), std::string(arg(1)));

    const auto settings = load_settings();
    int status = apply_display_mode(display.get(), settings) ? 0 : exit_failed;
    apply_blanking(display.get(), settings);
    if (arg(0) == "--allow-host" && !allow_host(display.get(), std::string(arg(1))))
        status = exit_not_allowed;
    if (!apply_keyboard(settings) && status == 0)
        status = exit_failed;
    return status;
}
