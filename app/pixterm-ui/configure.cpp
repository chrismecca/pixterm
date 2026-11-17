// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ui.hpp"

#include <host_list.hpp>

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/Separator.h>

namespace pixterm::ui {

Configure::Configure(App& app) : app_(app) {
    panels_.push_back(make_hosts_panel(app, *this));
    panels_.push_back(make_display_panel(app, *this));
    panels_.push_back(make_network_panel(app, *this));
    panels_.push_back(make_keyboard_panel(app, *this));
    panels_.push_back(make_system_panel(app, *this));
    panels_.push_back(make_diagnostics_panel(app, *this));
}

Widget Configure::dialog() {
    if (dialog_ == nullptr)
        build();
    return dialog_;
}

void Configure::open() {
    (void)dialog();
    select_row(categories_, shown_);
    show_panel(shown_);
    XtManageChild(dialog_);
}

void Configure::build() {
    Arg args[2];
    XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
    XtSetArg(args[1], XmNautoUnmanage, False);
    std::string name("configureDialog");
    dialog_ = XmCreateFormDialog(app_.shell, name.data(), args, 2);
    // A fixed size: changing panels must not resize the dialog.
    XtVaSetValues(dialog_, XmNwidth, 720, XmNheight, 480, XmNresizePolicy, XmRESIZE_NONE,
                  XmNhorizontalSpacing, 10, XmNverticalSpacing, 10, nullptr);
    XtVaSetValues(XtParent(dialog_), XmNtitle, "pixterm Configuration", nullptr);
    allow_move_only(XtParent(dialog_));

    Widget close = make_button(dialog_, "close", "Close", [this] {
        if (!pending_)
            XtUnmanageChild(dialog_);
    });
    XtVaSetValues(close, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, dialog_, XmNbottomAttachment,
                                XmATTACH_WIDGET, XmNbottomWidget, close, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);

    Widget category_frame = XtVaCreateManagedWidget(
        "categoryFrame", xmFrameWidgetClass, dialog_, XmNtopAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget,
        separator, XmNshadowType, XmSHADOW_IN, nullptr);
    categories_ = XtVaCreateManagedWidget("categories", xmListWidgetClass, category_frame,
                                          XmNselectionPolicy, XmBROWSE_SELECT, XmNvisibleItemCount,
                                          static_cast<int>(panels_.size()), XmNlistMarginWidth, 8,
                                          XmNlistMarginHeight, 6, nullptr);
    std::vector<std::string> titles;
    titles.reserve(panels_.size());
    for (const auto& p : panels_)
        titles.emplace_back(p->title());
    set_items(categories_, titles);
    on(categories_, XmNbrowseSelectionCallback, [this] {
        if (const auto row = selected_row(categories_))
            show_panel(*row);
    });

    Widget frame = XtVaCreateManagedWidget(
        "panelFrame", xmFrameWidgetClass, dialog_, XmNtopAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, category_frame, XmNrightAttachment,
        XmATTACH_FORM, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, separator,
        XmNshadowType, XmSHADOW_ETCHED_IN, nullptr);
    panel_title_ = XtVaCreateManagedWidget("panelTitle", xmLabelWidgetClass, frame, XmNchildType,
                                           XmFRAME_TITLE_CHILD, nullptr);
    Widget area =
        XtVaCreateManagedWidget("panelArea", xmFormWidgetClass, frame, XmNmarginWidth, 10,
                                XmNmarginHeight, 10, XmNresizePolicy, XmRESIZE_NONE, nullptr);
    for (const auto& p : panels_) {
        Widget w = p->build(area);
        XtVaSetValues(w, XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
        panel_widgets_.push_back(w);
    }
    XtVaSetValues(dialog_, XmNcancelButton, close, XmNinitialFocus, categories_, nullptr);
}

void Configure::show_panel(std::size_t index) {
    for (std::size_t i = 0; i < panel_widgets_.size(); ++i)
        if (i != index)
            XtUnmanageChild(panel_widgets_[i]);
    shown_ = index;
    set_label(panel_title_, panels_.at(index)->title());
    panels_.at(index)->refresh();
    XtManageChild(panel_widgets_.at(index));
}

void Configure::begin(Done done) {
    pending_ = true;
    done_ = std::move(done);
    XtSetSensitive(dialog(), False);
}

void Configure::commit(const Settings& next, Done done) {
    if (pending_)
        return;
    begin(std::move(done));
    if (!app_.apply(next)) {
        pending_ = false;
        done_ = nullptr;
        XtSetSensitive(dialog_, True);
    }
}

void Configure::request(chooser::Request request, Done done) {
    if (pending_)
        return;
    begin(std::move(done));
    if (!app_.send(chooser::request_line(request))) {
        pending_ = false;
        done_ = nullptr;
        XtSetSensitive(dialog_, True);
    }
}

void Configure::add_host(Host host, Done done) {
    auto edit = chooser::add_host(app_.settings(), std::move(host));
    if (!edit) {
        app_.error(edit.error());
        return;
    }
    commit(edit->settings, std::move(done));
}

void Configure::finished(bool ok, const std::string& text) {
    if (!pending_)
        return;
    pending_ = false;
    auto done = std::move(done_);
    done_ = nullptr;
    XtSetSensitive(dialog_, True);
    // Find Hosts adds hosts with the dialog closed.
    const bool open = XtIsManaged(dialog_) != False;
    Widget parent = open ? dialog_ : app_.shell;
    if (!ok)
        show_error(parent, text);
    else if (done)
        done(text);
    else if (!text.empty())
        show_info(parent, text);
    if (open)
        panels_.at(shown_)->refresh();
}

} // namespace pixterm::ui
