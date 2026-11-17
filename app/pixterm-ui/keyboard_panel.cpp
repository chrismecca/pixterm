// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Keyboard: layout and variant.

#include "ui.hpp"

#include <panels.hpp>

#include <Xm/Form.h>
#include <Xm/List.h>

namespace pixterm::ui {

namespace {

class KeyboardPanel final : public Panel {
public:
    KeyboardPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}

    [[nodiscard]] std::string_view title() const override { return "Keyboard"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget layouts_ = nullptr;
    Widget variant_ = nullptr;

    void apply();
};

Widget KeyboardPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("keyboardPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing,
                                   10, XmNverticalSpacing, 8, nullptr);
    Widget apply_button = make_button(form, "apply", "Apply", [this] { apply(); });
    XtVaSetValues(apply_button, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, nullptr);
    Widget note = make_label(form, "note", "Applies to this screen and to remote sessions.");
    XtVaSetValues(note, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  nullptr);
    Widget row =
        XtVaCreateManagedWidget("variantRow", xmFormWidgetClass, form, XmNbottomAttachment,
                                XmATTACH_WIDGET, XmNbottomWidget, apply_button, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    variant_ = place_row(row, nullptr, "Variant (optional):", make_text_field(row, "variant", 16));
    layouts_ = make_list(form, "layoutList");
    XtVaSetValues(XtParent(layouts_), XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment,
                  XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment,
                  XmATTACH_WIDGET, XmNbottomWidget, row, nullptr);
    std::vector<std::string> lines;
    lines.reserve(chooser::keyboard_layouts.size());
    for (const auto& layout : chooser::keyboard_layouts)
        lines.push_back(chooser::layout_line(layout));
    set_items(layouts_, lines);
    return form;
}

void KeyboardPanel::refresh() {
    if (const auto row = chooser::layout_row(app_.settings().keyboard_layout))
        select_row(layouts_, *row);
    else
        XmListDeselectAllItems(layouts_);
    set_text(variant_, app_.settings().keyboard_variant);
}

void KeyboardPanel::apply() {
    const auto row = selected_row(layouts_);
    const auto layout = row && *row < chooser::keyboard_layouts.size()
                            ? chooser::keyboard_layouts.at(*row).code
                            : std::string_view();
    auto chosen = chooser::read_keyboard(layout, text_of(variant_));
    if (!chosen) {
        show_error(configure_.dialog(), chosen.error());
        return;
    }
    Settings next = app_.settings();
    next.keyboard_layout = std::move(chosen->first);
    next.keyboard_variant = std::move(chosen->second);
    configure_.commit(next);
}

} // namespace

std::unique_ptr<Panel> make_keyboard_panel(App& app, Configure& configure) {
    return std::make_unique<KeyboardPanel>(app, configure);
}

} // namespace pixterm::ui
