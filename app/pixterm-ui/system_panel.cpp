// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > System: facts about the terminal, USB export and import,
// the administrator PIN, reset and restart.

#include "ui.hpp"

#include <pixterm/build_config.hpp>

#include <display.hpp>
#include <pin_gate.hpp>
#include <sys/utsname.h>
#include <system_info.hpp>

#include <Xm/Form.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>

#include <array>
#include <format>

namespace pixterm::ui {

namespace {

enum Fact : std::uint8_t {
    version,
    os,
    kernel,
    hardware,
    ip_address,
    mac_address,
    display,
    uptime,
    configuration,
    fact_count,
};

constexpr std::array<std::string_view, fact_count> fact_names{
    "pixterm:",     "Operating system:", "Kernel:", "Hardware:",      "IP address:",
    "MAC address:", "Display:",          "Uptime:", "Configuration:",
};

class SystemPanel final : public Panel {
public:
    SystemPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}

    [[nodiscard]] std::string_view title() const override { return "System"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    std::array<Widget, fact_count> values_{};
    Widget pin_dialog_ = nullptr;
    Widget pin_new_ = nullptr;
    Widget pin_again_ = nullptr;
    Widget pin_remove_ = nullptr;
    askpass::Secret new_pin_;
    askpass::Secret again_pin_;

    void build_pin_dialog();
    void open_pin_dialog();
    void clear_pins();
    void set_pin();
    void save_pin(std::string hash);
};

Widget SystemPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("systemPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing,
                                   10, XmNverticalSpacing, 4, nullptr);
    Widget above = nullptr;
    for (std::size_t i = 0; i < fact_count; ++i) {
        Widget key = make_label(form, "key", fact_names.at(i));
        Widget value = make_label(form, "value", "");
        XtVaSetValues(value, XmNtopAttachment, above != nullptr ? XmATTACH_WIDGET : XmATTACH_FORM,
                      XmNtopWidget, above, XmNleftAttachment, XmATTACH_POSITION, XmNleftPosition,
                      32, XmNrightAttachment, XmATTACH_FORM, nullptr);
        XtVaSetValues(key, XmNalignment, XmALIGNMENT_END, XmNtopAttachment,
                      XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, value, XmNleftAttachment,
                      XmATTACH_FORM, XmNrightAttachment, XmATTACH_POSITION, XmNrightPosition, 30,
                      nullptr);
        values_.at(i) = value;
        above = value;
    }

    Widget buttons = XtVaCreateManagedWidget(
        "systemButtons", xmRowColumnWidgetClass, form, XmNbottomAttachment, XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM, XmNorientation, XmHORIZONTAL, XmNspacing, 10, nullptr);
    make_button(buttons, "reset", "Reset Configuration...", [this] {
        confirm(configure_.dialog(),
                "Reset the configuration to the factory defaults?\n\n"
                "All hosts and device settings will be removed.",
                "Reset", [this] {
                    configure_.request(chooser::Request::reset, [this](const std::string&) {
                        show_info(configure_.dialog(),
                                  "The configuration was reset to the factory defaults.");
                    });
                });
    });
    make_button(buttons, "reboot", "Restart...", [this] {
        confirm(configure_.dialog(), "Restart the terminal now?", "Restart", [this] {
            if (app_.send(chooser::request_line(chooser::Request::reboot)))
                XtSetSensitive(configure_.dialog(), False);
        });
    });
    Widget more = XtVaCreateManagedWidget("systemButtons2", xmRowColumnWidgetClass, form,
                                          XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget,
                                          buttons, XmNrightAttachment, XmATTACH_FORM,
                                          XmNorientation, XmHORIZONTAL, XmNspacing, 10, nullptr);
    make_button(more, "export", "Export to USB",
                [this] { configure_.request(chooser::Request::export_settings); });
    make_button(more, "import", "Import from USB...", [this] {
        confirm(configure_.dialog(),
                "Replace this terminal's configuration with the one\n"
                "on the USB drive (pixterm-config.json)?",
                "Import", [this] { configure_.request(chooser::Request::import_settings); });
    });
    make_button(more, "pin", "Administrator PIN...", [this] { open_pin_dialog(); });
    return form;
}

void SystemPanel::refresh() {
    std::array<std::string, fact_count> v;
    v[version] = chooser::version_text(build::version, build::id);
    v[os] = chooser::os_name(chooser::small_file("/etc/os-release"));
    utsname u{};
    if (::uname(&u) == 0)
        v[kernel] =
            std::format("{} {} ({})", static_cast<const char*>(u.sysname),
                        static_cast<const char*>(u.release), static_cast<const char*>(u.machine));
    v[hardware] = chooser::first_line("/proc/device-tree/model");
    const auto address = chooser::primary_address();
    if (address)
        v[ip_address] = address->ipv4;
    v[mac_address] = chooser::first_line(
        std::format("/sys/class/net/{}/address", address ? address->interface : "eth0"));
    if (const auto m = x11::query_monitor(XtDisplay(app_.shell)); m && m->current)
        v[display] = mode_label(m->modes.at(*m->current));
    v[uptime] = chooser::uptime_text(chooser::small_file("/proc/uptime"));
    v[configuration] = chooser::settings_text(app_.settings().generation);
    for (std::size_t i = 0; i < fact_count; ++i)
        set_label(values_.at(i), v.at(i).empty() ? "unknown" : v.at(i));
}

// ---- the administrator PIN ----

void SystemPanel::build_pin_dialog() {
    pin_dialog_ = make_form_dialog(configure_.dialog(), "pinSetDialog", "Administrator PIN", false);
    pin_new_ =
        place_row(pin_dialog_, nullptr, "New PIN:", make_text_field(pin_dialog_, "pinNew", 14));
    pin_again_ = place_row(pin_dialog_, pin_new_,
                           "Repeat PIN:", make_text_field(pin_dialog_, "pinAgain", 14));
    attach_secret(pin_new_, new_pin_);
    attach_secret(pin_again_, again_pin_);
    Widget note = make_label(pin_dialog_, "note",
                             "4 to 12 digits. The PIN protects the\n"
                             "settings, not the terminal itself.");
    XtVaSetValues(note, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, pin_again_,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, pin_dialog_, XmNtopAttachment,
                                XmATTACH_WIDGET, XmNtopWidget, note, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget set = make_button(pin_dialog_, "set", "Set PIN", [this] { set_pin(); });
    pin_remove_ = make_button(pin_dialog_, "remove", "Remove PIN", [this] {
        clear_pins();
        XtUnmanageChild(pin_dialog_);
        save_pin("");
    });
    Widget cancel = make_button(pin_dialog_, "cancel", "Cancel", [this] {
        clear_pins();
        XtUnmanageChild(pin_dialog_);
    });
    XtVaSetValues(set, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(cancel, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, set, XmNbottomAttachment,
                  XmATTACH_FORM, nullptr);
    XtVaSetValues(pin_remove_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNleftAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(pin_dialog_, XmNdefaultButton, set, XmNcancelButton, cancel, XmNinitialFocus,
                  pin_new_, nullptr);
}

void SystemPanel::open_pin_dialog() {
    if (pin_dialog_ == nullptr)
        build_pin_dialog();
    XtSetSensitive(pin_remove_, static_cast<Boolean>(!app_.settings().admin_pin.empty()));
    XtManageChild(pin_dialog_);
}

void SystemPanel::clear_pins() {
    clear_secret(pin_new_, new_pin_);
    clear_secret(pin_again_, again_pin_);
}

void SystemPanel::set_pin() {
    auto hash = chooser::new_pin_hash(new_pin_.view(), again_pin_.view());
    clear_pins();
    if (!hash) {
        show_error(pin_dialog_, hash.error());
        return;
    }
    XtUnmanageChild(pin_dialog_);
    save_pin(std::move(*hash));
}

void SystemPanel::save_pin(std::string hash) {
    Settings next = app_.settings();
    next.admin_pin = std::move(hash);
    configure_.commit(next, [this](const std::string& /*text*/) {
        // Whoever set the PIN knows it.
        app_.unlock_admin();
        show_info(configure_.dialog(),
                  app_.settings().admin_pin.empty()
                      ? "The administrator PIN was removed."
                      : "The administrator PIN was set.\n\n"
                        "Configure now asks for it. If it is forgotten, create a file named\n"
                        "pixterm-reset on the SD card's boot partition to reset the terminal.");
    });
}

} // namespace

std::unique_ptr<Panel> make_system_panel(App& app, Configure& configure) {
    return std::make_unique<SystemPanel>(app, configure);
}

} // namespace pixterm::ui
