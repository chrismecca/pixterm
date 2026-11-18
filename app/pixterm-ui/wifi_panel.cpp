// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Wi-Fi: the country, a scan for networks, and the network
// to join with its passphrase. Wi-Fi gets its address by DHCP.

#include "ui.hpp"

#include <messages.hpp>
#include <panels.hpp>
#include <system_info.hpp>

#include <Xm/Form.h>
#include <Xm/List.h>
#include <Xm/Separator.h>
#include <Xm/ToggleB.h>

#include <format>
#include <string>
#include <vector>

namespace pixterm::ui {

namespace {

class WifiPanel final : public Panel {
public:
    WifiPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}
    ~WifiPanel() override = default;
    WifiPanel(const WifiPanel&) = delete;
    WifiPanel& operator=(const WifiPanel&) = delete;
    WifiPanel(WifiPanel&&) = delete;
    WifiPanel& operator=(WifiPanel&&) = delete;

    [[nodiscard]] std::string_view title() const override { return "Wi-Fi"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget status_ = nullptr;
    Widget use_ = nullptr;
    Widget country_ = nullptr;
    Widget scan_ = nullptr;
    Widget list_ = nullptr;
    Widget ssid_ = nullptr;
    Widget hidden_ = nullptr;
    Widget passphrase_field_ = nullptr;
    Widget apply_ = nullptr;
    askpass::Secret passphrase_;
    std::vector<WifiNetwork> found_;

    void sync();
    void scan();
    void pick();
    void apply();
};

Widget WifiPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("wifiPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing, 10,
                                   XmNverticalSpacing, 8, nullptr);
    status_ = make_label(form, "status", "");
    XtVaSetValues(status_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget separator = XtVaCreateManagedWidget(
        "separator", xmSeparatorWidgetClass, form, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget,
        status_, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    use_ = XtVaCreateManagedWidget("useWifi", xmToggleButtonWidgetClass, form, XmNtopAttachment,
                                   XmATTACH_WIDGET, XmNtopWidget, separator, XmNleftAttachment,
                                   XmATTACH_FORM, nullptr);
    set_label(use_, "Use Wi-Fi");
    on(use_, XmNvalueChangedCallback, [this] { sync(); });

    Widget top = XtVaCreateManagedWidget("countryRow", xmFormWidgetClass, form, XmNtopAttachment,
                                         XmATTACH_WIDGET, XmNtopWidget, use_, XmNleftAttachment,
                                         XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    country_ = place_row(top, nullptr, "Country (2 letters):", make_text_field(top, "country", 4));
    scan_ = make_button(top, "scan", "Scan", [this] { scan(); });
    XtVaSetValues(scan_, XmNtopAttachment, XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, country_,
                  XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, country_, XmNleftOffset, 12,
                  nullptr);

    apply_ = make_button(form, "apply", "Apply...", [this] { apply(); });
    XtVaSetValues(apply_, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  nullptr);
    Widget note = make_label(form, "note",
                             "Leave the passphrase empty to keep the one saved\n"
                             "for this network. Wi-Fi gets its address by DHCP.");
    XtVaSetValues(note, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, apply_, nullptr);

    Widget fields =
        XtVaCreateManagedWidget("networkRows", xmFormWidgetClass, form, XmNbottomAttachment,
                                XmATTACH_WIDGET, XmNbottomWidget, apply_, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    ssid_ = place_row(fields, nullptr, "Network name:", make_text_field(fields, "ssid", 28));
    hidden_ = XtVaCreateManagedWidget("hidden", xmToggleButtonWidgetClass, fields, nullptr);
    set_label(hidden_, "Hidden: it does not announce its name");
    XtVaSetValues(hidden_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, ssid_,
                  XmNleftAttachment, XmATTACH_POSITION, XmNleftPosition, 30, nullptr);
    passphrase_field_ =
        place_row(fields, hidden_, "Passphrase:", make_text_field(fields, "passphrase", 28));
    attach_secret(passphrase_field_, passphrase_);

    list_ = make_list(form, "wifiList");
    XtVaSetValues(XtParent(list_), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, top,
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, fields, XmNleftAttachment,
                  XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    on(list_, XmNbrowseSelectionCallback, [this] { pick(); });
    on(list_, XmNdefaultActionCallback, [this] { pick(); });
    return form;
}

void WifiPanel::refresh() {
    const auto link = chooser::wifi_interface();
    if (!link) {
        set_label(status_, "This terminal has no Wi-Fi.");
    } else {
        const auto address = chooser::interface_address(*link);
        set_label(status_, std::format("Interface:  {}\nCurrent address:  {}", *link,
                                       address ? address->ipv4 : "not connected"));
    }
    const auto f = chooser::wifi_form(app_.settings().wifi);
    set_toggle(use_, f.enabled);
    set_text(country_, f.country);
    set_text(ssid_, f.ssid);
    set_toggle(hidden_, f.hidden);
    clear_secret(passphrase_field_, passphrase_);
    for (Widget w : {use_, scan_, apply_})
        XtSetSensitive(w, static_cast<Boolean>(link.has_value()));
    sync();
}

void WifiPanel::sync() {
    const auto on = static_cast<Boolean>(toggle_state(use_));
    for (Widget w : {ssid_, hidden_, passphrase_field_, list_})
        XtSetSensitive(w, on);
}

void WifiPanel::scan() {
    const auto checked = chooser::read_wifi_form(
        {.enabled = false, .country = text_of(country_), .ssid = {}}, app_.settings().wifi, "");
    if (!checked || checked->country.empty()) {
        show_error(configure_.dialog(),
                   checked ? "Type the country first, as two letters, for example GB or US."
                           : checked.error());
        return;
    }
    set_text(country_, checked->country);
    set_label(status_, "Scanning for networks...");
    configure_.scan_wifi(checked->country, [this](const std::string& text) {
        found_ = parse_scan_reply(text);
        std::vector<std::string> rows;
        rows.reserve(found_.size());
        for (const auto& n : found_)
            rows.push_back(chooser::wifi_row(n));
        set_items(list_, rows);
        if (found_.empty())
            show_info(configure_.dialog(), "No WPA2 or WPA3 networks were found.");
    });
}

void WifiPanel::pick() {
    const auto row = selected_row(list_);
    if (!row || *row >= found_.size())
        return;
    set_text(ssid_, found_.at(*row).ssid);
    set_toggle(hidden_, false);
}

void WifiPanel::apply() {
    auto next = chooser::read_wifi_form({.enabled = toggle_state(use_),
                                         .country = text_of(country_),
                                         .ssid = text_of(ssid_),
                                         .hidden = toggle_state(hidden_)},
                                        app_.settings().wifi, passphrase_.view());
    if (!next) {
        show_error(configure_.dialog(), next.error());
        return;
    }
    Settings settings = app_.settings();
    settings.wifi = *next;
    const std::string question =
        next->enabled ? std::format("Join the Wi-Fi network {}?", next->ssid)
                      : std::string("Turn Wi-Fi off?\n\nThe saved passphrase is forgotten.");
    confirm(configure_.dialog(), question, "Apply", [this, settings] {
        configure_.commit(
            settings,
            [this](const std::string& /*text*/) {
                show_info(configure_.dialog(), "The Wi-Fi settings were applied.");
            },
            passphrase_.view());
        clear_secret(passphrase_field_, passphrase_);
    });
}

} // namespace

std::unique_ptr<Panel> make_wifi_panel(App& app, Configure& configure) {
    return std::make_unique<WifiPanel>(app, configure);
}

} // namespace pixterm::ui
