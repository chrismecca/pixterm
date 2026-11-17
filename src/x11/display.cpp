// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "display.hpp"

#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>

#include <cmath>
#include <span>
#include <utility>

namespace pixterm::x11 {

namespace {

template <auto Free>
struct Freer {
    template <typename T>
    void operator()(T* p) const noexcept {
        Free(p);
    }
};

using Resources = std::unique_ptr<XRRScreenResources, Freer<XRRFreeScreenResources>>;
using OutputInfo = std::unique_ptr<XRROutputInfo, Freer<XRRFreeOutputInfo>>;
using CrtcInfo = std::unique_ptr<XRRCrtcInfo, Freer<XRRFreeCrtcInfo>>;

std::string monitor_name(Display* d, RROutput output) {
    const Atom edid = XInternAtom(d, RR_PROPERTY_RANDR_EDID, True);
    if (edid == None)
        return {};
    unsigned char* data = nullptr;
    unsigned long count = 0;
    unsigned long after = 0;
    Atom type = None;
    int format = 0;
    if (XRRGetOutputProperty(d, output, edid, 0, 64, False, False, AnyPropertyType, &type, &format,
                             &count, &after, &data)
            != Success
        || data == nullptr)
        return {};
    auto name = edid_monitor_name({data, count});
    XFree(data);
    return name;
}

} // namespace

DisplayPtr open_display() {
    return DisplayPtr(XOpenDisplay(nullptr));
}

std::optional<Monitor> query_monitor(Display* d) {
    const Resources res(XRRGetScreenResourcesCurrent(d, DefaultRootWindow(d)));
    if (!res)
        return std::nullopt;
    const std::span outputs(res->outputs, static_cast<std::size_t>(res->noutput));
    const std::span all_modes(res->modes, static_cast<std::size_t>(res->nmode));
    for (const auto output : outputs) {
        const OutputInfo info(XRRGetOutputInfo(d, res.get(), output));
        if (!info || info->connection != RR_Connected || info->nmode == 0)
            continue;
        Monitor m;
        m.output = info->name;
        m.name = monitor_name(d, output);
        m.mm_width = info->mm_width;
        m.mm_height = info->mm_height;
        RRMode current = None;
        if (info->crtc != None) {
            if (const CrtcInfo crtc(XRRGetCrtcInfo(d, res.get(), info->crtc)); crtc)
                current = crtc->mode;
        }
        std::vector<Mode> modes;
        const std::span offered(info->modes, static_cast<std::size_t>(info->nmode));
        for (std::size_t i = 0; i < offered.size(); ++i) {
            for (const auto& mi : all_modes) {
                if (mi.id != offered[i] || (mi.modeFlags & RR_Interlace) != 0)
                    continue;
                modes.push_back(
                    {.id = mi.id,
                     .width = mi.width,
                     .height = mi.height,
                     .hz = refresh_rate({.dot_clock = mi.dotClock,
                                         .h_total = mi.hTotal,
                                         .v_total = mi.vTotal,
                                         .double_scan = (mi.modeFlags & RR_DoubleScan) != 0,
                                         .interlace = false}),
                     .preferred = std::cmp_less(i, info->npreferred)});
            }
        }
        m.modes = sorted_modes(modes, current);
        for (std::size_t i = 0; i < m.modes.size(); ++i)
            if (m.modes[i].id == current)
                m.current = i;
        return m;
    }
    return std::nullopt;
}

bool set_mode(Display* d, Monitor& monitor, std::size_t index) {
    if (index >= monitor.modes.size())
        return false;
    const Window root = DefaultRootWindow(d);
    const Resources res(XRRGetScreenResourcesCurrent(d, root));
    if (!res)
        return false;
    // Find the output and its crtc again by name.
    RROutput output = None;
    RRCrtc crtc = None;
    for (const auto o : std::span(res->outputs, static_cast<std::size_t>(res->noutput))) {
        const OutputInfo info(XRRGetOutputInfo(d, res.get(), o));
        if (!info || monitor.output != info->name)
            continue;
        output = o;
        crtc = info->crtc;
        if (crtc == None && info->ncrtc > 0)
            crtc = info->crtcs[0];
    }
    if (output == None || crtc == None)
        return false;
    const auto& m = monitor.modes[index];
    // At 96 dots per inch, which is what the old X programs expect.
    const auto mm = [](unsigned pixels) {
        return static_cast<int>(std::lround(pixels * 25.4 / 96.0));
    };
    // Turn the crtc off, resize the screen, then light it in the new mode.
    XGrabServer(d);
    XRRSetCrtcConfig(d, res.get(), crtc, CurrentTime, 0, 0, None, RR_Rotate_0, nullptr, 0);
    XRRSetScreenSize(d, root, static_cast<int>(m.width), static_cast<int>(m.height), mm(m.width),
                     mm(m.height));
    const bool ok =
        XRRSetCrtcConfig(d, res.get(), crtc, CurrentTime, 0, 0, m.id, RR_Rotate_0, &output, 1)
        == Success;
    XUngrabServer(d);
    XSync(d, False);
    if (ok)
        monitor.current = index;
    return ok;
}

} // namespace pixterm::x11
