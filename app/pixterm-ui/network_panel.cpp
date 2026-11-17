// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Network: DHCP or a manual address.

#include "ui.hpp"

#include <panels.hpp>
#include <system_info.hpp>

#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/ToggleB.h>

#include <format>

namespace pixterm::ui {

namespace {

class NetworkPanel final : public Panel {
public:
    NetworkPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}

    [[nodiscard]] std::string_view title() const override { return "Network"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget status_ = nullptr;
    Widget dhcp_ = nullptr;
    Widget manual_ = nullptr;
    Widget address_ = nullptr;
    Widget gateway_ = nullptr;
    Widget dns1_ = nullptr;
    Widget dns2_ = nullptr;

    void sync();
    void apply();
};

Widget NetworkPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("networkPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing,
                                   10, XmNverticalSpacing, 8, nullptr);
    status_ = make_label(form, "status", "");
    XtVaSetValues(status_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget separator = XtVaCreateManagedWidget(
        "separator", xmSeparatorWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget,
        status_, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);

    std::string name("method");
    Widget radio = XmCreateRadioBox(form, name.data(), nullptr, 0);
    XtVaSetValues(radio, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNleftAttachment, XmATTACH_FORM, XmNorientation, XmHORIZONTAL, nullptr);
    dhcp_ = XtVaCreateManagedWidget("dhcp", xmToggleButtonWidgetClass, radio, nullptr);
    set_label(dhcp_, "Automatic (DHCP)");
    manual_ = XtVaCreateManagedWidget("static", xmToggleButtonWidgetClass, radio, nullptr);
    set_label(manual_, "Manual");
    on(manual_, XmNvalueChangedCallback, [this] { sync(); });
    XtManageChild(radio);

    Widget fields = XtVaCreateManagedWidget(
        "fields", xmFormWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, radio,
        XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    address_ =
        place_row(fields, nullptr, "Address/prefix:", make_text_field(fields, "address", 20));
    gateway_ = place_row(fields, address_, "Gateway:", make_text_field(fields, "gateway", 20));
    dns1_ = place_row(fields, gateway_, "DNS server:", make_text_field(fields, "dns1", 20));
    dns2_ = place_row(fields, dns1_, "DNS server:", make_text_field(fields, "dns2", 20));

    Widget apply_button = make_button(form, "apply", "Apply...", [this] { apply(); });
    XtVaSetValues(apply_button, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, nullptr);
    Widget note = make_label(form, "note", "The terminal itself keeps working without a network.");
    XtVaSetValues(note, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  nullptr);
    return form;
}

void NetworkPanel::refresh() {
    const auto address = chooser::primary_address();
    const std::string interface = address ? address->interface : "eth0";
    const auto mac = chooser::first_line(std::format("/sys/class/net/{}/address", interface));
    set_label(status_, std::format("Interface:  {}     MAC address:  {}\nCurrent address:  {}",
                                   interface, mac.empty() ? "unknown" : mac,
                                   address ? address->ipv4 : "not connected"));
    const auto f = chooser::network_form(app_.settings().network);
    set_toggle(dhcp_, !f.is_static);
    set_toggle(manual_, f.is_static);
    set_text(address_, f.address);
    set_text(gateway_, f.gateway);
    set_text(dns1_, f.dns1);
    set_text(dns2_, f.dns2);
    sync();
}

void NetworkPanel::sync() {
    const auto manual = static_cast<Boolean>(toggle_state(manual_));
    for (Widget w : {address_, gateway_, dns1_, dns2_})
        XtSetSensitive(w, manual);
}

void NetworkPanel::apply() {
    const auto network = chooser::read_network_form({.is_static = toggle_state(manual_),
                                                     .address = text_of(address_),
                                                     .gateway = text_of(gateway_),
                                                     .dns1 = text_of(dns1_),
                                                     .dns2 = text_of(dns2_)});
    if (!network) {
        show_error(configure_.dialog(), network.error());
        return;
    }
    confirm(configure_.dialog(),
            "Apply these network settings?\n\n"
            "Connections may be interrupted while the network restarts.",
            "Apply", [this, n = *network] {
                Settings next = app_.settings();
                next.network = n;
                configure_.commit(next, [this](const std::string& /*text*/) {
                    show_info(configure_.dialog(), "The network settings were applied.");
                });
            });
}

} // namespace

std::unique_ptr<Panel> make_network_panel(App& app, Configure& configure) {
    return std::make_unique<NetworkPanel>(app, configure);
}

} // namespace pixterm::ui
