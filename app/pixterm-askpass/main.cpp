// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// pixterm-askpass: ssh's password and host key dialogs, in Motif.
//
// ssh runs it with the prompt as its argument and reads the answer from
// its standard output; pixtermd runs it the same way for rexec and RDP
// passwords. A password is kept only in a fixed buffer, shown as
// asterisks, written once and then wiped.
//
// Cancelling a password abandons the whole login: if the caller is ssh,
// askpass ends it, which pixtermd takes as a quiet cancellation (ssh
// would otherwise count a wrong password and ask again). Any other
// caller just sees askpass fail.

#include <pixterm/fd.hpp>
#include <pixterm/pin.hpp>

#include <askpass.hpp>
#include <fcntl.h>
#include <unistd.h>

#include <Xm/MessageB.h>
#include <Xm/MwmUtil.h>
#include <Xm/TextF.h>
#include <Xm/Xm.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <format>
#include <span>
#include <string>

using namespace pixterm;
using namespace pixterm::askpass;

namespace {

struct Dialog {
    XtAppContext app = nullptr;
    Widget shell = nullptr;
    Widget text = nullptr;
    Kind kind = Kind::secret;
    Secret secret;
    int status = 1;
};

void finish(Dialog& d, bool ok) {
    if (ok && d.kind == Kind::secret) {
        std::string line(d.secret.view());
        line += '\n';
        ok = write_all(STDOUT_FILENO, line).has_value();
        wipe(line);
    } else if (ok && d.kind == Kind::host_key) {
        ok = write_all(STDOUT_FILENO, "yes\n").has_value();
    }
    d.secret.clear();
    d.status = ok ? 0 : 1;
    XtAppSetExitFlag(d.app);
}

// Only ssh is ever ended this way.
bool parent_is_ssh() {
    const auto path = std::format("/proc/{}/comm", ::getppid());
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    return fd && read_all(fd.get(), 64).value_or("") == "ssh\n";
}

void on_ok(Widget /*w*/, XtPointer data, XtPointer /*call*/) {
    finish(*static_cast<Dialog*>(data), true);
}

void on_cancel(Widget /*w*/, XtPointer data, XtPointer /*call*/) {
    auto& d = *static_cast<Dialog*>(data);
    if (d.kind == Kind::secret && parent_is_ssh())
        ::kill(::getppid(), SIGTERM);
    finish(d, false);
}

// Keeps the secret in the buffer and lets the field show asterisks.
void on_modify(Widget /*w*/, XtPointer data, XtPointer call) {
    auto& d = *static_cast<Dialog*>(data);
    auto* edit = static_cast<XmTextVerifyCallbackStruct*>(call);
    const auto length =
        edit->text->ptr != nullptr ? static_cast<std::size_t>(edit->text->length) : 0;
    const std::span typed(edit->text->ptr, length);
    if (!d.secret.replace(static_cast<std::size_t>(edit->startPos),
                          static_cast<std::size_t>(edit->endPos),
                          std::string_view(typed.data(), typed.size()))) {
        edit->doit = False;
        return;
    }
    std::ranges::fill(typed, '*');
}

// Takes the keyboard as soon as the dialog shows, whatever the window
// manager does.
void on_map(Widget w, XtPointer data, XEvent* event, Boolean* /*go_on*/) {
    if (event->type != MapNotify)
        return;
    const auto& d = *static_cast<Dialog*>(data);
    XSetInputFocus(XtDisplay(w), XtWindow(w), RevertToParent, CurrentTime);
    if (d.text != nullptr)
        XmProcessTraversal(d.text, XmTRAVERSE_CURRENT);
    XtRemoveEventHandler(w, StructureNotifyMask, False, on_map, data);
}

XmString make_string(std::string text) {
    // Motif's interface takes a modifiable string, though it changes
    // nothing.
    return XmStringCreateLocalized(text.data());
}

unsigned char dialog_type(Kind kind) {
    switch (kind) {
    case Kind::secret:
        return XmDIALOG_MESSAGE;
    case Kind::host_key:
        return XmDIALOG_WARNING;
    case Kind::confirm:
        return XmDIALOG_QUESTION;
    case Kind::notice:
        break;
    }
    return XmDIALOG_INFORMATION;
}

} // namespace

int main(int argc, char** argv) {
    // On the terminal's own screen: for an 8-bit session, ssh's DISPLAY
    // is the display inside it, which is not up yet during the login.
    // Done first: setenv may invalidate pointers getenv returned.
    if (const char* own = std::getenv("PIXTERM_DISPLAY"); own != nullptr && *own != '\0')
        ::setenv("DISPLAY", own, 1);

    Dialog d;
    const std::string prompt = argc > 1 ? argv[1] : "Password:";
    const char* host = std::getenv("PIXTERM_HOST_NAME");
    d.kind = kind_of(prompt, std::getenv("SSH_ASKPASS_PROMPT"));

    XtSetLanguageProc(nullptr, nullptr, nullptr);
    d.shell = XtVaOpenApplication(&d.app, "PixtermAskpass", nullptr, 0, &argc, argv, nullptr,
                                  applicationShellWidgetClass, nullptr);
    const auto title =
        host != nullptr && *host != '\0' ? std::format("Login to {}", host) : std::string("Login");
    XtVaSetValues(d.shell, XmNtitle, title.c_str(), XmNdeleteResponse, XmDO_NOTHING,
                  XmNmwmDecorations, MWM_DECOR_BORDER | MWM_DECOR_TITLE, XmNmwmFunctions,
                  MWM_FUNC_MOVE, XmNmappedWhenManaged, False, nullptr);

    auto* const message = make_string(d.kind == Kind::host_key ? host_key_message(prompt) : prompt);
    Widget box = XtVaCreateManagedWidget("prompt", xmMessageBoxWidgetClass, d.shell, XmNdialogType,
                                         dialog_type(d.kind), XmNmessageString, message, nullptr);
    XmStringFree(message);
    XtUnmanageChild(XmMessageBoxGetChild(box, XmDIALOG_HELP_BUTTON));
    if (d.kind == Kind::host_key || d.kind == Kind::confirm) {
        auto* const yes = make_string("Yes");
        auto* const no = make_string("No");
        XtVaSetValues(box, XmNokLabelString, yes, XmNcancelLabelString, no, XmNdefaultButtonType,
                      XmDIALOG_CANCEL_BUTTON, nullptr);
        XmStringFree(yes);
        XmStringFree(no);
    } else if (d.kind == Kind::notice) {
        XtUnmanageChild(XmMessageBoxGetChild(box, XmDIALOG_OK_BUTTON));
        XtUnmanageChild(XmMessageBoxGetChild(box, XmDIALOG_CANCEL_BUTTON));
    }
    XtAddCallback(box, XmNokCallback, on_ok, &d);
    XtAddCallback(box, XmNcancelCallback, on_cancel, &d);
    if (d.kind == Kind::secret) {
        d.text =
            XtVaCreateManagedWidget("secret", xmTextFieldWidgetClass, box, XmNcolumns, 28,
                                    XmNmaxLength, static_cast<int>(Secret::capacity - 1), nullptr);
        XtAddCallback(d.text, XmNmodifyVerifyCallback, on_modify, &d);
        XtAddCallback(d.text, XmNactivateCallback, on_ok, &d);
        XtVaSetValues(box, XmNinitialFocus, d.text, nullptr);
    }
    XtAddEventHandler(d.shell, StructureNotifyMask, False, on_map, &d);
    XtRealizeWidget(d.shell);

    // In the middle of the screen.
    Dimension width = 0;
    Dimension height = 0;
    XtVaGetValues(d.shell, XmNwidth, &width, XmNheight, &height, nullptr);
    Screen* screen = XtScreen(d.shell);
    XtVaSetValues(d.shell, XmNx, (WidthOfScreen(screen) - width) / 2, XmNy,
                  (HeightOfScreen(screen) - height) / 2, nullptr);
    XtMapWidget(d.shell);
    XtAppMainLoop(d.app);
    d.secret.clear();
    return d.status;
}
