// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// pixterm-ui: the Motif host chooser, with its Configure dialog.
//
//     pixterm-ui
//         Shows the host list. It never starts sessions itself: it asks
//         pixtermd over the control socket ($PIXTERM_CONTROL_FD), and
//         exits when pixtermd hands the display to a remote session.
//     pixterm-ui --notice TEXT
//         Shows only TEXT in the middle of the screen until pixtermd
//         ends it, while a direct X session waits for its host.

#include "ui.hpp"

#include <pixterm/build_config.hpp>

#include <X11/cursorfont.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

using namespace pixterm;
using namespace pixterm::ui;

namespace {

// Set in the X resources (class Pixterm).
struct Resources {
    String root_pattern = nullptr; ///< root_weave, wide_weave, cross_weave or none
    Pixel root_foreground = 0;
    Pixel root_background = 0;
};

// Classic root window patterns from the X11 bitmaps collection.
constexpr std::array<unsigned char, 4> root_weave{0x07, 0x0d, 0x0b, 0x0e};
constexpr std::array<unsigned char, 32> wide_weave{
    0x11, 0x11, 0xb8, 0xb8, 0x7c, 0x7c, 0x3a, 0x3a, 0x11, 0x11, 0xa3, 0xa3, 0xc7, 0xc7, 0x8b, 0x8b,
    0x11, 0x11, 0xb8, 0xb8, 0x7c, 0x7c, 0x3a, 0x3a, 0x11, 0x11, 0xa3, 0xa3, 0xc7, 0xc7, 0x8b, 0x8b,
};
constexpr std::array<unsigned char, 32> cross_weave{
    0x55, 0x55, 0x88, 0x88, 0x55, 0x55, 0x22, 0x22, 0x55, 0x55, 0x88, 0x88, 0x55, 0x55, 0x22, 0x22,
    0x55, 0x55, 0x88, 0x88, 0x55, 0x55, 0x22, 0x22, 0x55, 0x55, 0x88, 0x88, 0x55, 0x55, 0x22, 0x22,
};

struct Pattern {
    std::string_view name;
    std::span<const unsigned char> bits;
    unsigned size;
};

constexpr std::array<Pattern, 3> patterns{{
    {.name = "root_weave", .bits = root_weave, .size = 4},
    {.name = "wide_weave", .bits = wide_weave, .size = 16},
    {.name = "cross_weave", .bits = cross_weave, .size = 16},
}};

std::string from_environment(const char* name, std::string_view fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? std::string(value) : std::string(fallback);
}

int control_fd() {
    const auto text = from_environment(std::string(control_fd_variable).c_str(), "");
    int fd = -1;
    if (const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), fd);
        text.empty() || ec != std::errc{} || ptr != text.data() + text.size())
        return -1;
    return fd;
}

Resources load_resources(Widget shell) {
    Resources r;
    // Xt's resource table holds modifiable strings: offsets into
    // Resources, and defaults as text.
    std::array<std::string, 9> text{"rootPattern",    "RootPattern",    "wide_weave",
                                    "rootForeground", "RootForeground", "black",
                                    "rootBackground", "RootBackground", "white"};
    const auto entry = [&text](std::size_t i, String type, std::size_t size, std::size_t offset) {
        return XtResource{.resource_name = text.at(i).data(),
                          .resource_class = text.at(i + 1).data(),
                          .resource_type = type,
                          .resource_size = static_cast<Cardinal>(size),
                          .resource_offset = static_cast<Cardinal>(offset),
                          .default_type = XmRString,
                          .default_addr = text.at(i + 2).data()};
    };
    std::array<XtResource, 3> table{
        entry(0, XmRString, sizeof(String), offsetof(Resources, root_pattern)),
        entry(3, XmRPixel, sizeof(Pixel), offsetof(Resources, root_foreground)),
        entry(6, XmRPixel, sizeof(Pixel), offsetof(Resources, root_background)),
    };
    XtGetApplicationResources(shell, &r, table.data(), table.size(), nullptr, 0);
    return r;
}

// Tiles the root window with a classic bitmap pattern, or fills it.
void paint_root(Display* display, const Resources& r) {
    const Window root = DefaultRootWindow(display);
    const std::string_view name = r.root_pattern != nullptr ? r.root_pattern : "";
    const auto* const p = std::ranges::find(patterns, name, &Pattern::name);
    if (p != patterns.end()) {
        std::string bits(p->bits.begin(), p->bits.end());
        const Pixmap tile = XCreatePixmapFromBitmapData(
            display, root, bits.data(), p->size, p->size, r.root_foreground, r.root_background,
            static_cast<unsigned>(DefaultDepth(display, DefaultScreen(display))));
        XSetWindowBackgroundPixmap(display, root, tile);
        XFreePixmap(display, tile);
    } else {
        XSetWindowBackground(display, root, r.root_background);
    }
    XClearWindow(display, root);
    XDefineCursor(display, root, XCreateFontCursor(display, XC_left_ptr));
}

int show_notice(XtAppContext context, Widget shell, std::string_view text) {
    XtVaSetValues(shell, XmNmwmDecorations, 0, XmNoverrideRedirect, True, nullptr);
    Widget frame = XtVaCreateManagedWidget("noticeFrame", xmFrameWidgetClass, shell, XmNshadowType,
                                           XmSHADOW_OUT, XmNshadowThickness, 2, nullptr);
    Widget label = XtVaCreateManagedWidget("notice", xmLabelWidgetClass, frame, XmNmarginWidth, 24,
                                           XmNmarginHeight, 18, nullptr);
    set_label(label, text);
    XtRealizeWidget(shell);
    Screen* screen = XtScreen(shell);
    centre(shell, WidthOfScreen(screen), HeightOfScreen(screen));
    XtMapWidget(shell);
    XtAppMainLoop(context);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    XtSetLanguageProc(nullptr, nullptr, nullptr);
    XtAppContext context = nullptr;
    Widget shell = XtVaOpenApplication(&context, "Pixterm", nullptr, 0, &argc, argv, nullptr,
                                       applicationShellWidgetClass, nullptr);
    paint_root(XtDisplay(shell), load_resources(shell));

    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 3 && std::string_view(args[1]) == "--notice")
        return show_notice(context, shell, args[2]);

    App app(context, shell,
            {.state_dir = from_environment("PIXTERM_STATE_DIR", build::state_dir),
             .factory_settings = from_environment("PIXTERM_CONFIG", build::factory_settings),
             .runtime_dir = from_environment("PIXTERM_RUNTIME_DIR", "")},
            control_fd());
    Launcher launcher(app);
    Configure configure(app);
    FindHosts find_hosts(app);
    app.launcher = &launcher;
    app.configure = &configure;
    app.find_hosts = &find_hosts;

    if (app.connected())
        XtAppAddInput(
            // Xt passes the condition mask in a pointer.
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            context, control_fd(), reinterpret_cast<XtPointer>(XtInputReadMask),
            [](XtPointer data, int* /*fd*/, XtInputId* /*id*/) {
                static_cast<App*>(data)->read_control();
            },
            &app);

    launcher.build();
    XtRealizeWidget(shell);
    XtMapWidget(shell);
    if (!app.connected())
        app.error("Not started by pixtermd: logins are disabled.");
    XtAppMainLoop(context);
    return app.exit_status;
}
