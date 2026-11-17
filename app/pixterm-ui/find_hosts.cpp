// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Find Hosts: the display managers on the local network that answer an
// XDMCP query. Connect starts a one-off session; Add to Hosts saves the
// host.

#include "ui.hpp"

#include <host_list.hpp>

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>

#include <format>

namespace pixterm::ui {

namespace {

// Broadcasts go out twice, as datagrams may be lost; the search ends
// after a few seconds.
constexpr unsigned long resend_ms = 1500;
constexpr unsigned long search_ms = 4000;

} // namespace

void FindHosts::open() {
    if (dialog_ == nullptr)
        build();
    XtManageChild(dialog_);
    start_search();
}

void FindHosts::build() {
    Arg args[2];
    XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
    XtSetArg(args[1], XmNautoUnmanage, False);
    std::string name("discoverDialog");
    dialog_ = XmCreateFormDialog(app_.shell, name.data(), args, 2);
    XtVaSetValues(dialog_, XmNwidth, 640, XmNheight, 400, XmNresizePolicy, XmRESIZE_NONE,
                  XmNhorizontalSpacing, 10, XmNverticalSpacing, 8, nullptr);
    XtVaSetValues(XtParent(dialog_), XmNtitle, "Find XDMCP Hosts", nullptr);
    allow_move_only(XtParent(dialog_));

    Widget close = make_button(dialog_, "close", "Close", [this] {
        stop();
        XtUnmanageChild(dialog_);
    });
    XtVaSetValues(close, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
                  nullptr);
    connect_ = make_button(dialog_, "connect", "Connect", [this] { connect(); });
    XtVaSetValues(connect_, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  nullptr);
    add_ = make_button(dialog_, "add", "Add to Hosts", [this] { add(); });
    XtVaSetValues(add_, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_WIDGET,
                  XmNleftWidget, connect_, nullptr);
    Widget again = make_button(dialog_, "again", "Search Again", [this] { start_search(); });
    XtVaSetValues(again, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_WIDGET,
                  XmNleftWidget, add_, nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, dialog_, XmNbottomAttachment,
                                XmATTACH_WIDGET, XmNbottomWidget, close, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);

    Widget query_button = make_button(dialog_, "query", "Query", [this] { query(); });
    XtVaSetValues(query_button, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, separator,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget query_label = make_label(dialog_, "queryLabel", "Query a host by name:");
    query_text_ = XtVaCreateManagedWidget(
        "queryHost", xmTextFieldWidgetClass, dialog_, XmNbottomAttachment, XmATTACH_WIDGET,
        XmNbottomWidget, separator, XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, query_label,
        XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, query_button, nullptr);
    XtVaSetValues(query_label, XmNleftAttachment, XmATTACH_FORM, XmNtopAttachment,
                  XmATTACH_OPPOSITE_WIDGET, XmNtopWidget, query_text_, XmNbottomAttachment,
                  XmATTACH_OPPOSITE_WIDGET, XmNbottomWidget, query_text_, nullptr);
    on(query_text_, XmNactivateCallback, [this] { query(); });

    status_ = make_label(dialog_, "status", "");
    XtVaSetValues(status_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    list_ = make_list(dialog_, "foundList");
    XtVaSetValues(XtParent(list_), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, status_,
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, query_text_,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    on(list_, XmNbrowseSelectionCallback, [this] { update_buttons(); });
    on(list_, XmNdefaultActionCallback, [this] { connect(); });
    XtVaSetValues(dialog_, XmNcancelButton, close, XmNinitialFocus, XtParent(list_), nullptr);
}

void FindHosts::start_search() {
    stop();
    found_.clear();
    redraw();
    auto search = chooser::XdmcpSearch::open(xdmcp::port);
    if (!search) {
        set_label(status_, search.error());
        return;
    }
    search_.emplace(std::move(*search));
    input_ = XtAppAddInput(
        // Xt passes the condition mask in a pointer.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        app_.context, search_->fd(), reinterpret_cast<XtPointer>(XtInputReadMask),
        [](XtPointer data, int* /*fd*/, XtInputId* /*id*/) {
            static_cast<FindHosts*>(data)->receive();
        },
        this);
    set_label(status_, "Searching the local network...");
    search_->broadcast();
    resend_ = XtAppAddTimeOut(
        app_.context, resend_ms,
        [](XtPointer data, XtIntervalId* /*id*/) {
            auto* self = static_cast<FindHosts*>(data);
            self->resend_ = 0;
            if (self->search_)
                self->search_->broadcast();
        },
        this);
    done_ = XtAppAddTimeOut(
        app_.context, search_ms,
        [](XtPointer data, XtIntervalId* /*id*/) { static_cast<FindHosts*>(data)->search_done(); },
        this);
}

void FindHosts::stop() {
    if (resend_ != 0)
        XtRemoveTimeOut(resend_);
    if (done_ != 0)
        XtRemoveTimeOut(done_);
    resend_ = 0;
    done_ = 0;
    if (search_) {
        XtRemoveInput(input_);
        input_ = 0;
        search_.reset();
    }
}

void FindHosts::search_done() {
    done_ = 0;
    set_label(status_, chooser::answered_text(found_.list().size()));
}

void FindHosts::receive() {
    if (!search_)
        return;
    bool changed = false;
    for (auto& f : search_->receive())
        changed = found_.add(std::move(f)) || changed;
    if (changed)
        redraw();
}

void FindHosts::redraw() {
    const auto row = selected_row(list_);
    std::vector<std::string> lines;
    for (const auto& f : found_.list())
        lines.push_back(chooser::found_line(f));
    set_items(list_, lines);
    if (!lines.empty())
        select_row(list_, row && *row < lines.size() ? *row : 0);
    update_buttons();
}

const chooser::Found* FindHosts::selected() const {
    const auto row = selected_row(list_);
    const auto& list = found_.list();
    return row && *row < list.size() ? &list[*row] : nullptr;
}

void FindHosts::update_buttons() {
    const auto* f = selected();
    XtSetSensitive(connect_, static_cast<Boolean>(f != nullptr && f->reply.willing));
    XtSetSensitive(add_, static_cast<Boolean>(f != nullptr));
}

void FindHosts::query() {
    if (!search_)
        start_search();
    if (!search_)
        return;
    if (auto sent = search_->query(text_of(query_text_)); !sent) {
        app_.error(sent.error());
        return;
    }
    if (done_ != 0)
        XtRemoveTimeOut(done_);
    set_label(status_, "Querying...");
    done_ = XtAppAddTimeOut(
        app_.context, search_ms,
        [](XtPointer data, XtIntervalId* /*id*/) { static_cast<FindHosts*>(data)->search_done(); },
        this);
}

void FindHosts::connect() {
    const auto* f = selected();
    if (f == nullptr || !f->reply.willing)
        return;
    const auto host = chooser::xdmcp_host(*f);
    stop();
    XtUnmanageChild(dialog_);
    app_.launcher->connect_query(host);
}

void FindHosts::add() {
    const auto* f = selected();
    if (f == nullptr)
        return;
    if (chooser::has_xdmcp_host(app_.settings(), f->address)) {
        app_.error("That host is already in the host list.");
        return;
    }
    app_.require_admin([this, host = chooser::xdmcp_host(*f)] {
        app_.configure->add_host(host, [this, name = host.name](const std::string& /*text*/) {
            set_label(status_, std::format("{} was added to the host list.", name));
        });
    });
}

} // namespace pixterm::ui
