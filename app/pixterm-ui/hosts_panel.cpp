// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Hosts: the host list, the host editor and the terminal's
// SSH key.

#include "ui.hpp"

#include <host_form.hpp>
#include <host_list.hpp>

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/Separator.h>
#include <Xm/Text.h>
#include <Xm/ToggleB.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace pixterm::ui {

namespace {

constexpr std::array session_types{SessionType::xsession, SessionType::xterm, SessionType::custom,
                                   SessionType::raw};

constexpr std::string_view editor_note =
    "Leave Username empty to be asked at login. A custom command\n"
    "runs through /bin/sh; a raw command is sent as typed (OpenVMS DCL).\n"
    "In a rexec or rsh command, %D stands for this terminal's display.\n"
    "Font server (optional): host or host:7100.\n"
    "XDMCP, Telnet, rexec and rsh are unencrypted: use them only on trusted networks.";

constexpr std::string_view key_note =
    "This terminal's public SSH key. To log in to a host with it, add\n"
    "this line to ~/.ssh/authorized_keys there (OpenVMS: [.SSH]AUTHORIZED_KEYS.\n"
    "in the login directory), then set the host to log in with the key.\n"
    "Export to USB also saves it, as pixterm-terminal-key.pub.\n\n"
    "Anyone at this terminal can then log in to those hosts as that user.";

Widget make_toggle(Widget parent, const char* name, std::string_view text) {
    Widget w = XtVaCreateManagedWidget(name, xmToggleButtonWidgetClass, parent, nullptr);
    set_label(w, text);
    return w;
}

void sensitive(Widget w, bool on) {
    XtSetSensitive(w, static_cast<Boolean>(on));
}

class HostsPanel final : public Panel {
public:
    HostsPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}

    [[nodiscard]] std::string_view title() const override { return "Hosts"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget list_ = nullptr;
    Widget edit_ = nullptr;
    Widget remove_ = nullptr;
    Widget up_ = nullptr;
    Widget down_ = nullptr;
    Widget forget_ = nullptr;
    std::optional<std::size_t> select_after_;

    // The editor.
    Widget editor_ = nullptr;
    Widget name_ = nullptr;
    Widget address_ = nullptr;
    Widget port_ = nullptr;
    Widget username_ = nullptr;
    Widget domain_ = nullptr;
    Widget device_ = nullptr;
    Widget speed_ = nullptr;
    Widget command_ = nullptr;
    Widget font_server_ = nullptr;
    Widget wake_mac_ = nullptr;
    Widget key_auth_ = nullptr;
    Widget allow_x_ = nullptr;
    Widget local_wm_ = nullptr;
    Widget indirect_ = nullptr;
    OptionMenu protocol_menu_;
    OptionMenu session_menu_;
    Protocol protocol_ = Protocol::ssh_x11;
    SessionType session_ = SessionType::xsession;
    std::optional<std::size_t> editing_; ///< the host being edited; none for a new one

    Widget key_dialog_ = nullptr;
    Widget key_text_ = nullptr;

    void build_editor();
    void open_editor(std::optional<std::size_t> index);
    void sync_editor();
    void protocol_chosen(std::size_t index);
    void save_editor();
    void commit(const chooser::Edit& edit, Configure::Done done = {});
    void remove_selected();
    void forget_key();
    void move_selected(int by);
    void show_key(const std::string& key);
};

Widget HostsPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("hostsPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing,
                                   10, XmNverticalSpacing, 8, nullptr);
    Widget buttons =
        XtVaCreateManagedWidget("hostButtons", xmRowColumnWidgetClass, form, XmNtopAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, XmNorientation,
                                XmVERTICAL, XmNpacking, XmPACK_COLUMN, XmNspacing, 6, nullptr);
    make_button(buttons, "add", "Add...", [this] { open_editor(std::nullopt); });
    edit_ = make_button(buttons, "edit", "Edit...", [this] {
        if (const auto row = selected_row(list_))
            open_editor(row);
    });
    remove_ = make_button(buttons, "remove", "Remove", [this] { remove_selected(); });
    up_ = make_button(buttons, "up", "Move Up", [this] { move_selected(-1); });
    down_ = make_button(buttons, "down", "Move Down", [this] { move_selected(1); });
    forget_ = make_button(buttons, "forget", "Forget Host Key...", [this] { forget_key(); });
    make_button(buttons, "key", "Terminal Key...", [this] {
        configure_.request(chooser::Request::keygen,
                           [this](const std::string& key) { show_key(key); });
    });

    list_ = make_list(form, "hostList");
    XtVaSetValues(XtParent(list_), XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment,
                  XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_WIDGET, XmNrightWidget, buttons, nullptr);
    on(list_, XmNbrowseSelectionCallback, [this] { refresh(); });
    on(list_, XmNdefaultActionCallback, [this] {
        if (const auto row = selected_row(list_))
            open_editor(row);
    });
    return form;
}

void HostsPanel::refresh() {
    const auto& hosts = app_.settings().hosts;
    auto row = select_after_ ? select_after_ : selected_row(list_);
    select_after_.reset();
    std::vector<std::string> lines;
    lines.reserve(hosts.size());
    for (const auto& h : hosts)
        lines.push_back(chooser::host_line(h));
    set_items(list_, lines);
    if (!hosts.empty()) {
        row = std::min(row.value_or(0), hosts.size() - 1);
        select_row(list_, *row);
    } else {
        row.reset();
    }
    const auto b = chooser::list_buttons(hosts.size(), row);
    sensitive(edit_, b.edit);
    sensitive(remove_, b.remove);
    sensitive(up_, b.up);
    sensitive(down_, b.down);
    sensitive(forget_, row && chooser::has_saved_key(hosts.at(*row)));
}

// A host reinstalled, or a Windows host whose certificate was renewed,
// shows a key the terminal refuses; forgetting it lets the next login
// trust the new one.
void HostsPanel::forget_key() {
    const auto row = selected_row(list_);
    const auto& hosts = app_.settings().hosts;
    if (!row || *row >= hosts.size() || !chooser::has_saved_key(hosts.at(*row)))
        return;
    const auto& host = hosts.at(*row);
    confirm(configure_.dialog(),
            std::format("Forget the host key of {}?\n\n"
                        "The next login trusts whatever key the host shows. Do this\n"
                        "only when you know why its key changed.",
                        host.name),
            "Forget", [this, id = host.id, name = host.name] {
                configure_.request_line(
                    chooser::forget_key_request(id), [this, name](const std::string& /*text*/) {
                        show_info(configure_.dialog(), std::format("The host key of {} was "
                                                                   "forgotten.",
                                                                   name));
                    });
            });
}

void HostsPanel::commit(const chooser::Edit& edit, Configure::Done done) {
    select_after_ = edit.select;
    configure_.commit(edit.settings, std::move(done));
}

void HostsPanel::remove_selected() {
    const auto row = selected_row(list_);
    if (!row || *row >= app_.settings().hosts.size())
        return;
    confirm(configure_.dialog(),
            std::format("Remove the host \"{}\"?", app_.settings().hosts[*row].name), "Remove",
            [this, index = *row] {
                if (index < app_.settings().hosts.size())
                    commit(chooser::remove_host(app_.settings(), index));
            });
}

void HostsPanel::move_selected(int by) {
    const auto row = selected_row(list_);
    if (!row)
        return;
    if (const auto edit = chooser::move_host(app_.settings(), *row, by))
        commit(*edit);
}

// ---- the editor ----

void HostsPanel::build_editor() {
    // Up until the host is saved: a mistake must not lose what was typed.
    editor_ = make_form_dialog(configure_.dialog(), "hostEditor", "Host", false);
    std::array<std::string_view, all_protocols.size()> protocol_labels{};
    for (std::size_t i = 0; i < all_protocols.size(); ++i)
        protocol_labels.at(i) = chooser::protocol_menu_label(all_protocols.at(i));
    std::array<std::string_view, session_types.size()> session_labels{};
    for (std::size_t i = 0; i < session_types.size(); ++i)
        session_labels.at(i) = session_label(session_types.at(i));

    name_ = place_row(editor_, nullptr, "Name:", make_text_field(editor_, "name", 28), true);
    address_ = place_row(editor_, name_, "Host:", make_text_field(editor_, "address", 28), true);
    protocol_menu_ = make_option_menu(editor_, "protocol", protocol_labels,
                                      [this](std::size_t i) { protocol_chosen(i); });
    place_row(editor_, address_, "Protocol:", protocol_menu_.menu);
    port_ = place_row(editor_, protocol_menu_.menu, "Port:", make_text_field(editor_, "port", 6));
    username_ = place_row(editor_, port_, "Username:", make_text_field(editor_, "username", 20));
    domain_ = place_row(editor_, username_, "Domain:", make_text_field(editor_, "domain", 20));
    device_ =
        place_row(editor_, domain_, "Serial device:", make_text_field(editor_, "device", 28), true);
    speed_ = place_row(editor_, device_, "Speed:", make_text_field(editor_, "speed", 8));
    session_menu_ = make_option_menu(editor_, "session", session_labels, [this](std::size_t i) {
        session_ = session_types.at(i);
        sync_editor();
    });
    place_row(editor_, speed_, "Session:", session_menu_.menu);
    command_ = place_row(editor_, session_menu_.menu,
                         "Command:", make_text_field(editor_, "command", 28), true);
    key_auth_ =
        place_row(editor_, command_,
                  "SSH login:", make_toggle(editor_, "keyAuth", "Log in with this terminal's key"));
    allow_x_ = place_row(
        editor_, key_auth_,
        "Terminal:", make_toggle(editor_, "allowX", "The host's X programs may open windows here"));
    local_wm_ = place_row(editor_, allow_x_, "Windows:",
                          make_toggle(editor_, "localWm", "Run mwm here (the host sends none)"));
    indirect_ =
        place_row(editor_, local_wm_,
                  "XDMCP:", make_toggle(editor_, "indirect", "Use the host's chooser (indirect)"));
    font_server_ = place_row(editor_, indirect_,
                             "Font server:", make_text_field(editor_, "fontServer", 28), true);
    wake_mac_ = place_row(editor_, font_server_,
                          "Wake-on-LAN MAC:", make_text_field(editor_, "wakeMac", 18));

    Widget note = make_label(editor_, "note", editor_note);
    XtVaSetValues(note, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, wake_mac_, XmNtopOffset,
                  10, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, editor_, XmNtopAttachment,
                                XmATTACH_WIDGET, XmNtopWidget, note, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget save = make_button(editor_, "save", "Save", [this] { save_editor(); });
    Widget cancel = make_button(editor_, "cancel", "Cancel", [this] { XtUnmanageChild(editor_); });
    XtVaSetValues(save, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(cancel, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, save, XmNbottomAttachment,
                  XmATTACH_FORM, nullptr);
    XtVaSetValues(editor_, XmNdefaultButton, save, XmNcancelButton, cancel, XmNinitialFocus, name_,
                  nullptr);
}

void HostsPanel::open_editor(std::optional<std::size_t> index) {
    if (editor_ == nullptr)
        build_editor();
    const auto& hosts = app_.settings().hosts;
    const Host* host = index && *index < hosts.size() ? &hosts[*index] : nullptr;
    editing_ = host != nullptr ? index : std::nullopt;
    const auto f = chooser::form_for(host);
    set_text(name_, f.name);
    set_text(address_, f.address);
    set_text(port_, f.port);
    set_text(username_, f.username);
    set_text(domain_, f.domain);
    set_text(device_, f.device);
    set_text(speed_, f.speed);
    set_text(command_, f.command);
    set_text(font_server_, f.font_server);
    set_text(wake_mac_, f.wake_mac);
    set_toggle(key_auth_, f.key_auth);
    set_toggle(allow_x_, f.allow_x);
    set_toggle(local_wm_, f.local_wm);
    set_toggle(indirect_, f.indirect);
    protocol_ = f.protocol;
    session_ = f.session;
    sync_editor();
    XtVaSetValues(XtParent(editor_), XmNtitle, host != nullptr ? "Edit Host" : "Add Host", nullptr);
    XtManageChild(editor_);
}

void HostsPanel::sync_editor() {
    const auto e = chooser::enabled_fields(protocol_, session_);
    sensitive(address_, e.address);
    sensitive(port_, e.port);
    sensitive(device_, e.device);
    sensitive(speed_, e.speed);
    sensitive(username_, e.username);
    sensitive(domain_, e.domain);
    sensitive(key_auth_, e.key_auth);
    sensitive(allow_x_, e.allow_x);
    sensitive(local_wm_, e.local_wm);
    sensitive(session_menu_.menu, e.session);
    sensitive(command_, e.command);
    sensitive(indirect_, e.indirect);
    sensitive(font_server_, e.font_server);
    sensitive(wake_mac_, e.wake_mac);
    protocol_menu_.show(static_cast<std::size_t>(protocol_));
    session_menu_.show(static_cast<std::size_t>(std::ranges::find(session_types, session_)
                                                - session_types.begin()));
}

void HostsPanel::protocol_chosen(std::size_t index) {
    protocol_ = all_protocols.at(index);
    set_text(port_, chooser::port_after_change(text_of(port_), protocol_));
    sync_editor();
}

void HostsPanel::save_editor() {
    const chooser::HostForm form{.name = text_of(name_),
                                 .address = text_of(address_),
                                 .port = text_of(port_),
                                 .username = text_of(username_),
                                 .domain = text_of(domain_),
                                 .device = text_of(device_),
                                 .speed = text_of(speed_),
                                 .command = text_of(command_),
                                 .font_server = text_of(font_server_),
                                 .wake_mac = text_of(wake_mac_),
                                 .protocol = protocol_,
                                 .session = session_,
                                 .key_auth = toggle_state(key_auth_),
                                 .allow_x = toggle_state(allow_x_),
                                 .indirect = toggle_state(indirect_),
                                 .local_wm = toggle_state(local_wm_)};
    auto host = chooser::read_form(form);
    if (!host) {
        show_error(editor_, host.error());
        return;
    }
    const auto close = [this](const std::string& /*text*/) { XtUnmanageChild(editor_); };
    if (editing_ && *editing_ < app_.settings().hosts.size()) {
        commit(chooser::replace_host(app_.settings(), *editing_, std::move(*host)), close);
        return;
    }
    auto edit = chooser::add_host(app_.settings(), std::move(*host));
    if (!edit) {
        show_error(editor_, edit.error());
        return;
    }
    commit(*edit, close);
}

// ---- the terminal's SSH key ----

void HostsPanel::show_key(const std::string& key) {
    if (key.empty()) {
        show_error(configure_.dialog(), "The terminal key could not be read.");
        return;
    }
    if (key_dialog_ == nullptr) {
        key_dialog_ = make_form_dialog(configure_.dialog(), "keyDialog", "Terminal Key");
        Widget note = make_label(key_dialog_, "note", key_note);
        XtVaSetValues(note, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                      XmNrightAttachment, XmATTACH_FORM, nullptr);
        Arg args[5];
        XtSetArg(args[0], XmNeditMode, XmMULTI_LINE_EDIT);
        XtSetArg(args[1], XmNeditable, False);
        XtSetArg(args[2], XmNwordWrap, True);
        XtSetArg(args[3], XmNrows, 3);
        XtSetArg(args[4], XmNcolumns, 60);
        std::string name("keyText");
        key_text_ = XmCreateText(key_dialog_, name.data(), args, 5);
        XtVaSetValues(key_text_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, note,
                      XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
        XtManageChild(key_text_);
        Widget close =
            make_button(key_dialog_, "close", "Close", [this] { XtUnmanageChild(key_dialog_); });
        XtVaSetValues(close, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, key_text_,
                      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
                      nullptr);
        XtVaSetValues(key_dialog_, XmNdefaultButton, close, XmNcancelButton, close, nullptr);
    }
    // One line, without the newline ssh-keygen ends it with.
    std::string line = key.substr(0, key.find('\n'));
    XmTextSetString(key_text_, line.data());
    XtManageChild(key_dialog_);
}

} // namespace

std::unique_ptr<Panel> make_hosts_panel(App& app, Configure& configure) {
    return std::make_unique<HostsPanel>(app, configure);
}

} // namespace pixterm::ui
