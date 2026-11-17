// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "motif.hpp"

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/MwmUtil.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <algorithm>
#include <memory>
#include <string>

namespace pixterm::ui {

namespace {

void run_action(Widget /*w*/, XtPointer data, XtPointer /*call*/) {
    (*static_cast<Action*>(data))();
}

void delete_action(Widget /*w*/, XtPointer data, XtPointer /*call*/) {
    delete static_cast<Action*>(data);
}

void destroy_shell(Widget w, XtPointer /*data*/, XtPointer /*call*/) {
    XtDestroyWidget(w);
}

// A message dialog that destroys itself when it is taken down.
Widget make_message(Widget parent, const char* name, unsigned char type, std::string_view text) {
    Arg args[3];
    XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
    XtSetArg(args[1], XmNnoResize, True);
    XtSetArg(args[2], XmNdialogType, type);
    std::string n(name);
    Widget d = XmCreateMessageDialog(parent, n.data(), args, 3);
    const CompoundString message(text);
    XtVaSetValues(d, XmNmessageString, message.get(), nullptr);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_HELP_BUTTON));
    XtVaSetValues(XtParent(d), XmNtitle, "pixterm", nullptr);
    allow_move_only(XtParent(d));
    XtAddCallback(XtParent(d), XmNpopdownCallback, destroy_shell, nullptr);
    return d;
}

void secret_modified(Widget /*w*/, XtPointer data, XtPointer call) {
    auto& secret = *static_cast<askpass::Secret*>(data);
    auto* edit = static_cast<XmTextVerifyCallbackStruct*>(call);
    const auto length =
        edit->text->ptr != nullptr ? static_cast<std::size_t>(edit->text->length) : 0;
    const std::span typed(edit->text->ptr, length);
    if (!secret.replace(static_cast<std::size_t>(edit->startPos),
                        static_cast<std::size_t>(edit->endPos),
                        std::string_view(typed.data(), typed.size()))) {
        edit->doit = False;
        return;
    }
    std::ranges::fill(typed, '*');
}

} // namespace

CompoundString::CompoundString(std::string_view text) {
    // Motif's interface takes a modifiable string, though it changes
    // nothing.
    std::string copy(text);
    string_ = XmStringCreateLocalized(copy.data());
}

void on(Widget w, const char* callback, Action action) {
    auto* owned = new Action(std::move(action));
    XtAddCallback(w, callback, run_action, owned);
    XtAddCallback(w, XmNdestroyCallback, delete_action, owned);
}

void set_label(Widget w, std::string_view text) {
    const CompoundString s(text);
    XtVaSetValues(w, XmNlabelString, s.get(), nullptr);
}

Widget make_label(Widget parent, const char* name, std::string_view text) {
    Widget w = XtVaCreateManagedWidget(name, xmLabelWidgetClass, parent, XmNalignment,
                                       XmALIGNMENT_BEGINNING, nullptr);
    set_label(w, text);
    return w;
}

Widget make_button(Widget parent, const char* name, std::string_view text, Action action) {
    Widget w = XtVaCreateManagedWidget(name, xmPushButtonWidgetClass, parent, nullptr);
    set_label(w, text);
    if (action)
        on(w, XmNactivateCallback, std::move(action));
    return w;
}

Widget make_text_field(Widget parent, const char* name, short columns) {
    return XtVaCreateManagedWidget(name, xmTextFieldWidgetClass, parent, XmNcolumns, columns,
                                   nullptr);
}

std::string text_of(Widget field) {
    const std::unique_ptr<char, decltype(&XtFree)> s(XmTextFieldGetString(field), &XtFree);
    return s ? std::string(s.get()) : std::string();
}

void set_text(Widget field, std::string_view text) {
    std::string copy(text);
    XmTextFieldSetString(field, copy.data());
}

bool toggle_state(Widget toggle) {
    return XmToggleButtonGetState(toggle) != False;
}

void set_toggle(Widget toggle, bool on) {
    XmToggleButtonSetState(toggle, on ? True : False, False);
}

Widget place_row(Widget form, Widget above, std::string_view label, Widget field, bool stretch) {
    Widget l = XtVaCreateManagedWidget(
        "rowLabel", xmLabelWidgetClass, form, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment,
        XmATTACH_POSITION, XmNrightPosition, 28, XmNalignment, XmALIGNMENT_END, nullptr);
    set_label(l, label);
    XtVaSetValues(field, XmNtopAttachment, above != nullptr ? XmATTACH_WIDGET : XmATTACH_FORM,
                  XmNtopWidget, above, XmNtopOffset, 6, XmNleftAttachment, XmATTACH_POSITION,
                  XmNleftPosition, 30, XmNrightAttachment, stretch ? XmATTACH_FORM : XmATTACH_NONE,
                  nullptr);
    XtVaSetValues(l, XmNtopAttachment, XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, field,
                  XmNbottomAttachment, XmATTACH_OPPOSITE_WIDGET, XmNbottomWidget, field, nullptr);
    return field;
}

void OptionMenu::show(std::size_t index) const {
    if (index < items.size())
        XtVaSetValues(menu, XmNmenuHistory, items[index], nullptr);
}

OptionMenu make_option_menu(Widget parent, const char* name,
                            std::span<const std::string_view> labels,
                            const std::function<void(std::size_t)>& chosen) {
    OptionMenu m;
    std::string pulldown_name("pulldown");
    Widget pulldown = XmCreatePulldownMenu(parent, pulldown_name.data(), nullptr, 0);
    for (std::size_t i = 0; i < labels.size(); ++i)
        m.items.push_back(make_button(pulldown, "item", labels[i], [chosen, i] { chosen(i); }));
    Arg args[1];
    XtSetArg(args[0], XmNsubMenuId, pulldown);
    std::string n(name);
    m.menu = XmCreateOptionMenu(parent, n.data(), args, 1);
    XtUnmanageChild(XmOptionLabelGadget(m.menu));
    XtManageChild(m.menu);
    return m;
}

Widget make_list(Widget parent, const char* name) {
    Arg args[3];
    XtSetArg(args[0], XmNselectionPolicy, XmBROWSE_SELECT);
    XtSetArg(args[1], XmNscrollBarDisplayPolicy, XmAS_NEEDED);
    XtSetArg(args[2], XmNlistSizePolicy, XmCONSTANT);
    std::string n(name);
    Widget list = XmCreateScrolledList(parent, n.data(), args, 3);
    XtManageChild(list);
    return list;
}

void set_items(Widget list, std::span<const std::string> items) {
    XmListDeleteAllItems(list);
    for (const auto& item : items) {
        const CompoundString s(item);
        XmListAddItemUnselected(list, s.get(), 0);
    }
}

std::optional<std::size_t> selected_row(Widget list) {
    int* positions = nullptr;
    int count = 0;
    std::optional<std::size_t> row;
    if (XmListGetSelectedPos(list, &positions, &count) != False && count > 0 && positions[0] >= 1)
        row = static_cast<std::size_t>(positions[0] - 1);
    XtFree(reinterpret_cast<char*>(positions));
    return row;
}

void select_row(Widget list, std::size_t row) {
    const int position = static_cast<int>(row) + 1;
    XmListSelectPos(list, position, False);
    int top = 0;
    int visible = 0;
    XtVaGetValues(list, XmNtopItemPosition, &top, XmNvisibleItemCount, &visible, nullptr);
    if (position < top)
        XmListSetPos(list, position);
    else if (position >= top + visible)
        XmListSetBottomPos(list, position);
}

void allow_move_only(Widget shell) {
    XtVaSetValues(shell, XmNdeleteResponse, XmDO_NOTHING, XmNmwmDecorations,
                  MWM_DECOR_BORDER | MWM_DECOR_TITLE, XmNmwmFunctions, MWM_FUNC_MOVE, nullptr);
}

void centre(Widget shell, int screen_width, int screen_height) {
    Dimension width = 0;
    Dimension height = 0;
    XtVaGetValues(shell, XmNwidth, &width, XmNheight, &height, nullptr);
    XtVaSetValues(shell, XmNx, (screen_width - width) / 2, XmNy, (screen_height - height) / 2,
                  nullptr);
}

Widget make_form_dialog(Widget parent, const char* name, std::string_view title,
                        bool close_on_button) {
    Arg args[3];
    XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
    XtSetArg(args[1], XmNnoResize, True);
    // Motif reads this only when the dialog is made.
    XtSetArg(args[2], XmNautoUnmanage, close_on_button ? True : False);
    std::string n(name);
    Widget d = XmCreateFormDialog(parent, n.data(), args, 3);
    XtVaSetValues(d, XmNhorizontalSpacing, 10, XmNverticalSpacing, 8, XmNmarginWidth, 12,
                  XmNmarginHeight, 12, nullptr);
    const std::string t(title);
    XtVaSetValues(XtParent(d), XmNtitle, t.c_str(), nullptr);
    allow_move_only(XtParent(d));
    return d;
}

void show_error(Widget parent, std::string_view text) {
    Widget d = make_message(parent, "errorDialog", XmDIALOG_ERROR, text);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    XtManageChild(d);
}

void show_info(Widget parent, std::string_view text) {
    Widget d = make_message(parent, "infoDialog", XmDIALOG_INFORMATION, text);
    XtUnmanageChild(XmMessageBoxGetChild(d, XmDIALOG_CANCEL_BUTTON));
    XtManageChild(d);
}

void confirm(Widget parent, std::string_view text, std::string_view ok_label, Action yes) {
    Widget d = make_message(parent, "confirmDialog", XmDIALOG_QUESTION, text);
    const CompoundString ok(ok_label);
    XtVaSetValues(d, XmNokLabelString, ok.get(), XmNdefaultButtonType, XmDIALOG_CANCEL_BUTTON,
                  nullptr);
    on(d, XmNokCallback, std::move(yes));
    XtManageChild(d);
}

void attach_secret(Widget field, askpass::Secret& secret) {
    XtAddCallback(field, XmNmodifyVerifyCallback, secret_modified, &secret);
}

void clear_secret(Widget field, askpass::Secret& secret) {
    // The secret takes the deletion first, so nothing is left of it.
    set_text(field, "");
    secret.clear();
}

} // namespace pixterm::ui
