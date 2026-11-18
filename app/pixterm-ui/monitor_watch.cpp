// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// A monitor plugged in after Xorg started, or after a start with none,
// stays dark until a client lights it. While the chooser is up, it
// lights such a monitor in the saved mode, or the automatic one, and
// keeps its windows in the middle of the screen when the size changes.

#include "ui.hpp"

#include <display.hpp>

#include <X11/extensions/Xrandr.h>

namespace pixterm::ui {

namespace {

// How long the outputs must be quiet first: one plug brings a burst of
// events.
constexpr unsigned long settle_ms = 500;

// Xt's event dispatchers take no closure.
struct Watch {
    App* app = nullptr;
    int event_base = 0;
    XtIntervalId settling = 0;
};

Watch& watch() {
    static Watch w;
    return w;
}

void light_monitor(XtPointer /*data*/, XtIntervalId* /*id*/) {
    auto& w = watch();
    w.settling = 0;
    if (w.app == nullptr)
        return;
    Display* d = XtDisplay(w.app->shell);
    auto monitor = x11::query_monitor(d);
    if (!monitor)
        return;
    const auto wanted = wanted_mode(*monitor, w.app->settings().display_mode);
    if (wanted && wanted != monitor->current)
        (void)x11::set_mode(d, *monitor, *wanted);
}

Boolean dispatch(XEvent* event) {
    auto& w = watch();
    if (w.app == nullptr)
        return False;
    if (event->type == w.event_base + RRScreenChangeNotify) {
        XRRUpdateConfiguration(event);
        Screen* screen = XtScreen(w.app->shell);
        centre(w.app->shell, WidthOfScreen(screen), HeightOfScreen(screen));
        if (w.app->configure != nullptr)
            w.app->configure->screen_changed();
    } else {
        if (w.settling != 0)
            XtRemoveTimeOut(w.settling);
        w.settling = XtAppAddTimeOut(w.app->context, settle_ms, light_monitor, nullptr);
    }
    return True;
}

} // namespace

void watch_monitors(App& app) {
    Display* d = XtDisplay(app.shell);
    int error_base = 0;
    auto& w = watch();
    if (XRRQueryExtension(d, &w.event_base, &error_base) == False)
        return;
    w.app = &app;
    (void)XtSetEventDispatcher(d, w.event_base + RRScreenChangeNotify, dispatch);
    (void)XtSetEventDispatcher(d, w.event_base + RRNotify, dispatch);
    XRRSelectInput(d, DefaultRootWindow(d), RRScreenChangeNotifyMask | RROutputChangeNotifyMask);
}

} // namespace pixterm::ui
