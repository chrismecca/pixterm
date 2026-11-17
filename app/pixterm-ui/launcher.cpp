// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ui.hpp"

#include <pixterm/build_config.hpp>

#include <host_form.hpp>
#include <system_info.hpp>

#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/MwmUtil.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>

#include <algorithm>
#include <format>
#include <memory>

namespace pixterm::ui {

namespace {

// How often the status line looks at the network.
constexpr unsigned long network_poll_ms = 3000;

// A third button of the row along the bottom, from left to right
// position (out of 100).
void place_button(Widget button, Widget above, int left, int right) {
    XtVaSetValues(button, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, above,
                  XmNleftAttachment, XmATTACH_POSITION, XmNleftPosition, left, XmNrightAttachment,
                  XmATTACH_POSITION, XmNrightPosition, right, XmNdefaultButtonShadowThickness, 1,
                  nullptr);
}

} // namespace

void Launcher::build() {
    Screen* screen = XtScreen(app_.shell);
    const int sw = WidthOfScreen(screen);
    const int sh = HeightOfScreen(screen);
    const auto w = static_cast<Dimension>(std::clamp(sw * 2 / 5, 420, 720));
    const auto h = static_cast<Dimension>(std::clamp(sh / 2, 340, 560));

    XtVaSetValues(app_.shell, XmNtitle, "pixterm - Network Terminal", XmNwidth, w, XmNheight, h,
                  XmNx, (sw - w) / 2, XmNy, (sh - h) / 2, XmNmappedWhenManaged, False, nullptr);
    allow_move_only(app_.shell);

    Widget form =
        XtVaCreateManagedWidget("launcher", xmFormWidgetClass, app_.shell, XmNhorizontalSpacing, 12,
                                XmNverticalSpacing, 10, nullptr);
    Widget title = make_label(form, "title", "Available Hosts");
    XtVaSetValues(title, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  nullptr);

    // The status line along the bottom.
    Widget version = make_label(form, "version", std::format("pixterm {}", build::version));
    XtVaSetValues(version, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  nullptr);
    network_ = make_label(form, "network", "");
    XtVaSetValues(network_, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  XmNalignment, XmALIGNMENT_END, nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, form, XmNbottomAttachment,
                                XmATTACH_WIDGET, XmNbottomWidget, version, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);

    login_ = make_button(form, "login", "Login", [this] { login(); });
    place_button(login_, separator, 4, 32);
    find_ = make_button(form, "find", "Find Hosts...", [this] {
        if (!connecting_)
            app_.find_hosts->open();
    });
    place_button(find_, separator, 36, 64);
    Widget configure = make_button(form, "configure", "Configure...", [this] {
        if (!connecting_)
            app_.require_admin([this] { app_.configure->open(); });
    });
    place_button(configure, separator, 68, 96);

    Widget frame = XtVaCreateManagedWidget("hostFrame", xmFrameWidgetClass, form, XmNtopAttachment,
                                           XmATTACH_WIDGET, XmNtopWidget, title, XmNleftAttachment,
                                           XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                                           XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget,
                                           login_, XmNshadowType, XmSHADOW_IN, nullptr);
    Widget inner = XtVaCreateManagedWidget("hostArea", xmFormWidgetClass, frame, nullptr);
    host_list_ = make_list(inner, "hosts");
    XtVaSetValues(XtParent(host_list_), XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment,
                  XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, nullptr);
    on(host_list_, XmNbrowseSelectionCallback, [this] { update_buttons(); });
    on(host_list_, XmNdefaultActionCallback, [this] { login(); });
    empty_label_ =
        XtVaCreateWidget("emptyLabel", xmLabelWidgetClass, inner, XmNtopAttachment, XmATTACH_FORM,
                         XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                         XmNrightAttachment, XmATTACH_FORM, nullptr);
    set_label(empty_label_, "No hosts configured.\n\nSelect Configure to add a host.");

    // No default button: Return in the list already means Login, and a
    // default button would let Motif fire the same Return again on
    // whichever button takes the focus when Login turns insensitive.
    XtVaSetValues(form, XmNinitialFocus, frame, nullptr);
    XtVaSetValues(inner, XmNinitialFocus, XtParent(host_list_), nullptr);

    show_hosts();
    update_network();
}

void Launcher::show_hosts() {
    const auto& hosts = app_.settings().hosts;
    std::vector<std::string> names;
    names.reserve(hosts.size());
    for (const auto& h : hosts)
        names.push_back(h.name);
    set_items(host_list_, names);
    if (hosts.empty()) {
        XtUnmanageChild(XtParent(host_list_));
        XtManageChild(empty_label_);
    } else {
        XtUnmanageChild(empty_label_);
        XtManageChild(XtParent(host_list_));
        select_row(host_list_, 0);
    }
    update_buttons();
}

const Host* Launcher::selected_host() const {
    const auto row = selected_row(host_list_);
    const auto& hosts = app_.settings().hosts;
    return row && *row < hosts.size() ? &hosts[*row] : nullptr;
}

void Launcher::update_buttons() {
    XtSetSensitive(login_, static_cast<Boolean>(!connecting_ && selected_host() != nullptr));
    XtSetSensitive(find_, static_cast<Boolean>(!connecting_));
    XtSetSensitive(host_list_, static_cast<Boolean>(!connecting_));
}

void Launcher::login() {
    const auto* h = selected_host();
    if (h == nullptr || connecting_)
        return;
    if (chooser::needs_username(*h))
        ask_username(*h);
    else
        start(*h, "");
}

void Launcher::start(const Host& host, std::string_view username) {
    if (app_.send(chooser::start_request(host.id, username)))
        show_connecting(host.name);
}

void Launcher::connect_query(const Host& found) {
    if (app_.send(chooser::query_request(found.address, found.name)))
        show_connecting(found.name);
}

void Launcher::show_connecting(std::string_view name) {
    connecting_ = true;
    update_buttons();
    if (working_ == nullptr) {
        Arg args[2];
        XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
        // Up until pixtermd answers, whatever the user presses.
        XtSetArg(args[1], XmNautoUnmanage, False);
        std::string dialog_name("connectingDialog");
        working_ = XmCreateWorkingDialog(app_.shell, dialog_name.data(), args, 2);
        XtUnmanageChild(XmMessageBoxGetChild(working_, XmDIALOG_OK_BUTTON));
        XtUnmanageChild(XmMessageBoxGetChild(working_, XmDIALOG_HELP_BUTTON));
        allow_move_only(XtParent(working_));
        on(working_, XmNcancelCallback, [this] {
            if (connecting_)
                (void)app_.send(chooser::request_line(chooser::Request::cancel));
            XtSetSensitive(XmMessageBoxGetChild(working_, XmDIALOG_CANCEL_BUTTON), False);
        });
    }
    const CompoundString message(std::format("Connecting to {}...", name));
    XtVaSetValues(working_, XmNmessageString, message.get(), nullptr);
    XtSetSensitive(XmMessageBoxGetChild(working_, XmDIALOG_CANCEL_BUTTON), True);
    XtManageChild(working_);
}

void Launcher::idle() {
    connecting_ = false;
    if (working_ != nullptr)
        XtUnmanageChild(working_);
    update_buttons();
    XmProcessTraversal(host_list_, XmTRAVERSE_CURRENT);
}

void Launcher::ask_username(const Host& host) {
    if (user_prompt_ == nullptr) {
        Arg args[1];
        XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
        std::string dialog_name("userDialog");
        user_prompt_ = XmCreatePromptDialog(app_.shell, dialog_name.data(), args, 1);
        XtUnmanageChild(XmSelectionBoxGetChild(user_prompt_, XmDIALOG_HELP_BUTTON));
        allow_move_only(XtParent(user_prompt_));
        XtAddCallback(
            user_prompt_, XmNokCallback,
            [](Widget /*w*/, XtPointer data, XtPointer call) {
                static_cast<Launcher*>(data)->username_entered(
                    static_cast<XmSelectionBoxCallbackStruct*>(call)->value);
            },
            this);
    }
    const CompoundString prompt(std::format("User name on {}:", host.name));
    const CompoundString empty("");
    XtVaSetValues(user_prompt_, XmNselectionLabelString, prompt.get(), XmNtextString, empty.get(),
                  nullptr);
    XtManageChild(user_prompt_);
}

void Launcher::username_entered(XmString value) {
    const auto* h = selected_host();
    if (h == nullptr)
        return;
    char* raw = nullptr;
    const bool converted = XmStringGetLtoR(value, XmFONTLIST_DEFAULT_TAG, &raw) != False;
    const std::unique_ptr<char, decltype(&XtFree)> text(raw, &XtFree);
    const std::string name = converted && text ? text.get() : "";
    if (!is_valid_username(name)) {
        app_.error("Please enter a valid user name.");
        return;
    }
    start(*h, name);
}

void Launcher::update_network() {
    set_label(network_, chooser::network_status(chooser::primary_address()));
    XtAppAddTimeOut(
        app_.context, network_poll_ms,
        [](XtPointer data, XtIntervalId* /*id*/) {
            static_cast<Launcher*>(data)->update_network();
        },
        this);
}

} // namespace pixterm::ui
