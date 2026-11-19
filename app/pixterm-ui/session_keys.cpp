// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// The console during a session (pixterm-ui --session HOST). pixtermd
// runs it in the chooser's place while a session uses the local display.
// It shows nothing until Ctrl+Alt+Home, which opens a small dialog:
//
//   Lock Screen  asks for a code (4 to 12 digits), then covers the screen
//                and holds the keyboard and pointer until the code is
//                typed again. Three wrong codes wait half a minute. A
//                forgotten code: Ctrl+Alt+Backspace ends the session.
//   Disconnect   ends the session (pixtermd hears DISCONNECT).
//   Cancel       goes back to the session.
//
// There is no window manager during a session, so the dialogs take the
// keyboard focus themselves.

#include "ui.hpp"

#include <pixterm/lines.hpp>

#include <pin_gate.hpp>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/Separator.h>

#include <array>
#include <format>
#include <string>

namespace pixterm::ui {

namespace {

constexpr KeySym hot_key = XK_Home;
constexpr unsigned hot_modifiers = ControlMask | Mod1Mask;
constexpr unsigned long grab_retry_ms = 200;

class SessionKeys {
public:
    SessionKeys(XtAppContext context, Widget shell, std::string host, int control_fd)
        : context_(context), shell_(shell), host_(std::move(host)), control_fd_(control_fd) {}

    void grab_hot_key();
    // A key on the root window: the hot key, through the passive grab.
    [[nodiscard]] bool hot_key_pressed(const XKeyEvent& e) const;
    void open_console();

private:
    XtAppContext context_;
    Widget shell_;
    std::string host_;
    int control_fd_;
    Widget console_ = nullptr;
    Widget code_dialog_ = nullptr;
    Widget code_ = nullptr;
    Widget code_again_ = nullptr;
    askpass::Secret new_code_;
    askpass::Secret new_code_again_;
    Widget lock_shell_ = nullptr;
    Widget lock_message_ = nullptr;
    Widget unlock_field_ = nullptr;
    askpass::Secret typed_;
    std::string code_hash_;
    chooser::PinGate gate_;

    [[nodiscard]] Display* display() const { return XtDisplay(shell_); }
    void build_console();
    void build_code_dialog();
    void close_console();
    void choose_code();
    void lock();
    void grab_for_lock();
    void try_unlock();
    void disconnect();
    // The dialog's window gets the keyboard once it is on screen.
    static void focus_when_mapped(Widget shell);
    void centre_on_screen(Widget shell);
};

void SessionKeys::grab_hot_key() {
    Display* d = display();
    const KeyCode code = XKeysymToKeycode(d, hot_key);
    if (code == 0)
        return;
    // Caps Lock and Num Lock must not hide the key.
    for (const unsigned locks :
         {0U, unsigned{LockMask}, unsigned{Mod2Mask}, unsigned{LockMask} | unsigned{Mod2Mask}})
        XGrabKey(d, code, hot_modifiers | locks, DefaultRootWindow(d), True, GrabModeAsync,
                 GrabModeAsync);
    XSync(d, False);
}

bool SessionKeys::hot_key_pressed(const XKeyEvent& e) const {
    return e.window == DefaultRootWindow(display())
           && e.keycode == XKeysymToKeycode(display(), hot_key)
           && (e.state & hot_modifiers) == hot_modifiers;
}

void SessionKeys::centre_on_screen(Widget shell) {
    Screen* screen = XtScreen(shell_);
    centre(shell, WidthOfScreen(screen), HeightOfScreen(screen));
}

void SessionKeys::focus_when_mapped(Widget shell) {
    XtAddEventHandler(
        shell, StructureNotifyMask, False,
        [](Widget w, XtPointer, XEvent* event, Boolean*) {
            if (event->type == MapNotify)
                XSetInputFocus(XtDisplay(w), XtWindow(w), RevertToPointerRoot, CurrentTime);
        },
        nullptr);
}

void SessionKeys::build_console() {
    console_ = make_form_dialog(shell_, "consoleDialog", "pixterm", false);
    Widget text = make_label(console_, "consoleText", std::format("Session on {}", host_));
    XtVaSetValues(text, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, console_, XmNtopAttachment,
                                XmATTACH_WIDGET, XmNtopWidget, text, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget lock_button = make_button(console_, "lock", "Lock Screen", [this] {
        close_console();
        choose_code();
    });
    Widget disconnect_button = make_button(console_, "disconnect", "Disconnect", [this] {
        close_console();
        disconnect();
    });
    Widget cancel = make_button(console_, "cancel", "Cancel", [this] { close_console(); });
    XtVaSetValues(lock_button, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNleftAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(disconnect_button, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, lock_button,
                  XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(cancel, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNleftAttachment, XmATTACH_WIDGET, XmNleftWidget, disconnect_button,
                  XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(console_, XmNdefaultButton, cancel, XmNcancelButton, cancel, XmNinitialFocus,
                  cancel, nullptr);
    focus_when_mapped(XtParent(console_));
}

void SessionKeys::open_console() {
    if (lock_shell_ != nullptr || (console_ != nullptr && XtIsManaged(console_) != False)
        || (code_dialog_ != nullptr && XtIsManaged(code_dialog_) != False))
        return;
    if (console_ == nullptr)
        build_console();
    XtManageChild(console_);
    centre_on_screen(XtParent(console_));
}

void SessionKeys::close_console() {
    XtUnmanageChild(console_);
    XSetInputFocus(display(), PointerRoot, RevertToPointerRoot, CurrentTime);
}

void SessionKeys::build_code_dialog() {
    code_dialog_ = make_form_dialog(shell_, "lockCodeDialog", "Lock the screen", false);
    code_ = place_row(code_dialog_, nullptr, "Code:", make_text_field(code_dialog_, "code", 14));
    code_again_ = place_row(code_dialog_, code_,
                            "Repeat code:", make_text_field(code_dialog_, "codeAgain", 14));
    attach_secret(code_, new_code_);
    attach_secret(code_again_, new_code_again_);
    Widget note = make_label(code_dialog_, "note",
                             "4 to 12 digits, to unlock the screen with. If you forget\n"
                             "it, Ctrl+Alt+Backspace ends the session.");
    XtVaSetValues(note, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, code_again_,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget separator =
        XtVaCreateManagedWidget("separator", xmSeparatorWidgetClass, code_dialog_, XmNtopAttachment,
                                XmATTACH_WIDGET, XmNtopWidget, note, XmNleftAttachment,
                                XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget lock_button = make_button(code_dialog_, "lock", "Lock", [this] { lock(); });
    Widget cancel = make_button(code_dialog_, "cancel", "Cancel", [this] {
        clear_secret(code_, new_code_);
        clear_secret(code_again_, new_code_again_);
        XtUnmanageChild(code_dialog_);
        XSetInputFocus(display(), PointerRoot, RevertToPointerRoot, CurrentTime);
    });
    XtVaSetValues(lock_button, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(cancel, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, separator,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, lock_button,
                  XmNbottomAttachment, XmATTACH_FORM, nullptr);
    XtVaSetValues(code_dialog_, XmNdefaultButton, lock_button, XmNcancelButton, cancel,
                  XmNinitialFocus, code_, nullptr);
    focus_when_mapped(XtParent(code_dialog_));
}

void SessionKeys::choose_code() {
    if (code_dialog_ == nullptr)
        build_code_dialog();
    XtManageChild(code_dialog_);
    centre_on_screen(XtParent(code_dialog_));
}

void SessionKeys::lock() {
    auto hash = chooser::new_pin_hash(new_code_.view(), new_code_again_.view());
    clear_secret(code_, new_code_);
    clear_secret(code_again_, new_code_again_);
    if (!hash) {
        show_error(code_dialog_, hash.error());
        return;
    }
    XtUnmanageChild(code_dialog_);
    code_hash_ = std::move(*hash);
    gate_ = chooser::PinGate();

    Screen* screen = XtScreen(shell_);
    lock_shell_ = XtVaCreatePopupShell(
        "lockScreen", topLevelShellWidgetClass, shell_, XmNoverrideRedirect, True, XmNx, 0, XmNy, 0,
        XmNwidth, WidthOfScreen(screen), XmNheight, HeightOfScreen(screen), nullptr);
    Widget area = XtVaCreateManagedWidget("lockArea", xmFormWidgetClass, lock_shell_, XmNbackground,
                                          BlackPixelOfScreen(screen), nullptr);
    Widget box = XtVaCreateManagedWidget(
        "lockBox", xmFormWidgetClass, area, XmNtopAttachment, XmATTACH_POSITION, XmNtopPosition, 40,
        XmNleftAttachment, XmATTACH_POSITION, XmNleftPosition, 30, XmNrightAttachment,
        XmATTACH_POSITION, XmNrightPosition, 70, XmNshadowThickness, 2, XmNshadowType, XmSHADOW_OUT,
        XmNmarginWidth, 16, XmNmarginHeight, 16, nullptr);
    lock_message_ = make_label(box, "lockText",
                               std::format("The session on {} is locked.\n"
                                           "Type the code to unlock it.",
                                           host_));
    XtVaSetValues(lock_message_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    unlock_field_ = make_text_field(box, "unlockCode", 14);
    XtVaSetValues(unlock_field_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, lock_message_,
                  XmNtopOffset, 10, XmNleftAttachment, XmATTACH_POSITION, XmNleftPosition, 30,
                  XmNbottomAttachment, XmATTACH_FORM, nullptr);
    attach_secret(unlock_field_, typed_);
    on(unlock_field_, XmNactivateCallback, [this] { try_unlock(); });
    XtPopup(lock_shell_, XtGrabNone);
    // Xt names no override-redirect window; name this one, so that it
    // can be found (by the session test, for one).
    std::string name("lockScreen");
    std::string app_class("Pixterm");
    XClassHint hint{.res_name = name.data(), .res_class = app_class.data()};
    XSetClassHint(display(), XtWindow(lock_shell_), &hint);
    XStoreName(display(), XtWindow(lock_shell_), "pixterm lock");
    grab_for_lock();
}

// The session's programs may hold the keyboard or pointer for a moment:
// try until both are ours.
void SessionKeys::grab_for_lock() {
    if (lock_shell_ == nullptr)
        return;
    Display* d = display();
    const Window w = XtWindow(lock_shell_);
    XRaiseWindow(d, w);
    const bool keyboard =
        XGrabKeyboard(d, w, True, GrabModeAsync, GrabModeAsync, CurrentTime) == GrabSuccess;
    const bool pointer = XGrabPointer(d, w, True, ButtonPressMask | ButtonReleaseMask,
                                      GrabModeAsync, GrabModeAsync, w, None, CurrentTime)
                         == GrabSuccess;
    XSetInputFocus(d, XtWindow(unlock_field_), RevertToParent, CurrentTime);
    XmProcessTraversal(unlock_field_, XmTRAVERSE_CURRENT);
    if (!keyboard || !pointer)
        XtAppAddTimeOut(
            context_, grab_retry_ms,
            [](XtPointer data, XtIntervalId*) { static_cast<SessionKeys*>(data)->grab_for_lock(); },
            this);
}

void SessionKeys::try_unlock() {
    const auto result = gate_.enter(typed_.view(), code_hash_, chooser::Clock::now());
    clear_secret(unlock_field_, typed_);
    switch (result) {
    case chooser::PinGate::Result::unlocked: {
        Display* d = display();
        XUngrabKeyboard(d, CurrentTime);
        XUngrabPointer(d, CurrentTime);
        XtDestroyWidget(lock_shell_);
        lock_shell_ = nullptr;
        code_hash_.clear();
        XSetInputFocus(d, PointerRoot, RevertToPointerRoot, CurrentTime);
        XSync(d, False);
        return;
    }
    case chooser::PinGate::Result::wrong:
        set_label(lock_message_, std::format("The session on {} is locked.\n"
                                             "That code is wrong. Type it again.",
                                             host_));
        return;
    case chooser::PinGate::Result::locked_out:
        set_label(lock_message_, std::format("The session on {} is locked.\n"
                                             "Too many wrong codes: wait half a minute.",
                                             host_));
        return;
    }
}

void SessionKeys::disconnect() {
    if (control_fd_ < 0 || !send_line(control_fd_, "DISCONNECT"))
        show_error(shell_, "pixtermd could not be reached.");
}

// Xt's dispatcher for key presses takes no closure.
SessionKeys*& keys() {
    static SessionKeys* k = nullptr;
    return k;
}

XtEventDispatchProc& default_key_dispatch() {
    static XtEventDispatchProc proc = nullptr;
    return proc;
}

Boolean dispatch_key(XEvent* event) {
    if (keys() != nullptr && keys()->hot_key_pressed(event->xkey)) {
        keys()->open_console();
        return True;
    }
    if (default_key_dispatch() == nullptr)
        return False;
    return default_key_dispatch()(event);
}

} // namespace

int run_session_keys(XtAppContext context, Widget shell, std::string host, int control_fd) {
    SessionKeys session(context, shell, std::move(host), control_fd);
    keys() = &session;
    // A shell that stays unmapped, as the dialogs' parent.
    XtVaSetValues(shell, XmNmappedWhenManaged, False, XmNwidth, 1, XmNheight, 1, nullptr);
    XtRealizeWidget(shell);
    default_key_dispatch() = XtSetEventDispatcher(XtDisplay(shell), KeyPress, dispatch_key);
    session.grab_hot_key();
    XtAppMainLoop(context);
    keys() = nullptr;
    return 0;
}

} // namespace pixterm::ui
