// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Diagnostics: the supervisor's recent messages.

#include "ui.hpp"

#include <system_info.hpp>

#include <Xm/Form.h>
#include <Xm/Text.h>

namespace pixterm::ui {

namespace {

class DiagnosticsPanel final : public Panel {
public:
    DiagnosticsPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}

    [[nodiscard]] std::string_view title() const override { return "Diagnostics"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget text_ = nullptr;
};

Widget DiagnosticsPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("diagPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing, 10,
                                   XmNverticalSpacing, 8, nullptr);
    Widget again = make_button(form, "refresh", "Refresh", [this] { refresh(); });
    XtVaSetValues(again, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  nullptr);
    Widget save = make_button(form, "saveDiagnostics", "Save to USB", [this] {
        configure_.request(chooser::Request::diagnostics, [this](const std::string& text) {
            show_info(configure_.dialog(), text);
        });
    });
    XtVaSetValues(save, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_WIDGET,
                  XmNrightWidget, again, nullptr);
    Widget note = make_label(form, "note", "Recent messages from the terminal supervisor.");
    XtVaSetValues(note, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, save, nullptr);
    Arg args[5];
    XtSetArg(args[0], XmNeditMode, XmMULTI_LINE_EDIT);
    XtSetArg(args[1], XmNeditable, False);
    XtSetArg(args[2], XmNcursorPositionVisible, False);
    XtSetArg(args[3], XmNscrollHorizontal, False);
    XtSetArg(args[4], XmNwordWrap, True);
    std::string name("diagText");
    text_ = XmCreateScrolledText(form, name.data(), args, 5);
    XtVaSetValues(XtParent(text_), XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment,
                  XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment,
                  XmATTACH_WIDGET, XmNbottomWidget, again, nullptr);
    XtManageChild(text_);
    return form;
}

void DiagnosticsPanel::refresh() {
    auto log = chooser::small_file(app_.paths().runtime_dir / "pixtermd.log");
    if (log.empty())
        log = "No messages yet.";
    XmTextSetString(text_, log.data());
    XmTextShowPosition(text_, XmTextGetLastPosition(text_));
}

} // namespace

std::unique_ptr<Panel> make_diagnostics_panel(App& app, Configure& configure) {
    return std::make_unique<DiagnosticsPanel>(app, configure);
}

} // namespace pixterm::ui
