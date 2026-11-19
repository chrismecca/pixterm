// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Time: the time zone and NTP servers. The terminal has no
// battery for its clock, so it sets the clock from the network at every
// start; secure connections (RDP, TN3270 and TN5250 over TLS) need it.

#include "ui.hpp"

#include <panels.hpp>

#include <Xm/Form.h>

#include <array>
#include <ctime>
#include <format>
#include <string>

namespace pixterm::ui {

namespace {

class TimePanel final : public Panel {
public:
    TimePanel(App& app, Configure& configure) : app_(app), configure_(configure) {}

    [[nodiscard]] std::string_view title() const override { return "Time"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget now_ = nullptr;
    Widget zone_ = nullptr;
    Widget ntp1_ = nullptr;
    Widget ntp2_ = nullptr;

    void apply();
};

Widget TimePanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("timePanel", xmFormWidgetClass, parent, XmNhorizontalSpacing, 10,
                                   XmNverticalSpacing, 8, nullptr);
    now_ = make_label(form, "now", "");
    XtVaSetValues(now_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget fields = XtVaCreateManagedWidget(
        "fields", xmFormWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, now_,
        XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    zone_ = place_row(fields, nullptr, "Time zone:", make_text_field(fields, "zone", 24));
    ntp1_ = place_row(fields, zone_, "NTP server:", make_text_field(fields, "ntp1", 24));
    ntp2_ = place_row(fields, ntp1_, "NTP server:", make_text_field(fields, "ntp2", 24));
    Widget apply_button = make_button(form, "apply", "Apply", [this] { apply(); });
    XtVaSetValues(apply_button, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, nullptr);
    Widget note = make_label(form, "note",
                             "Zones are named by region and city, such as Europe/London.\n"
                             "Leave the servers empty to use the network's own, or Debian's.");
    XtVaSetValues(note, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, apply_button, nullptr);
    return form;
}

void TimePanel::refresh() {
    // The zone may have changed since this program started.
    ::tzset();
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    std::array<char, 64> text{};
    if (::localtime_r(&t, &local) != nullptr)
        (void)std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M %Z", &local);
    set_label(now_, std::format("Now:  {}", text.data()));
    const auto f = chooser::time_form(app_.settings().time);
    set_text(zone_, f.zone);
    set_text(ntp1_, f.ntp1);
    set_text(ntp2_, f.ntp2);
}

void TimePanel::apply() {
    const auto time = chooser::read_time_form(
        {.zone = text_of(zone_), .ntp1 = text_of(ntp1_), .ntp2 = text_of(ntp2_)});
    if (!time) {
        show_error(configure_.dialog(), time.error());
        return;
    }
    Settings next = app_.settings();
    next.time = *time;
    configure_.commit(next);
}

} // namespace

std::unique_ptr<Panel> make_time_panel(App& app, Configure& configure) {
    return std::make_unique<TimePanel>(app, configure);
}

} // namespace pixterm::ui
