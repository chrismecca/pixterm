// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Remote desktops: VNC through TigerVNC's viewer, and RDP through
// FreeRDP, each filling the screen.

#include "parts.hpp"
#include "sessions.hpp"

#include <pixterm/pin.hpp>

#include <filesystem>
#include <format>

namespace pixterm::supervisor {

namespace {

constexpr Duration rdp_login_timeout{30'000};

// VNC: the server decides how to log in (no password, a VNC password,
// or a user name and password, as macOS Screen Sharing asks), so the
// viewer asks with its own dialog. Its F8 menu can end the session.
class VncSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view /*username*/) override {
        if (!probe_host(sup(), [this](bool reachable) {
                if (reachable)
                    sup().start_handoff();
            })) {
            sup().report_error("Something else is still in progress; try again.");
            return false;
        }
        sup().set_state(State::probe);
        return true;
    }

    void cancel() override {
        sup().log(std::format("connection to {} cancelled", sup().host().name));
        sup().system().cancel_job();
        sup().set_state(State::local);
        sup().chooser_idle();
    }

    // The viewer's full-screen mode needs a window manager, and there is
    // none during a session, so its window is made the screen's size.
    void start() override {
        const auto sized = [this](ExitStatus status, std::string_view output) {
            if (sup().state() != State::handoff)
                return;
            const auto [w, h] = screen_size(status, output);
            const auto& host = sup().host();
            // Connection problems end the viewer instead of offering to
            // reconnect: the session ends and the chooser explains.
            run_desktop(sup(), {.name = "vncviewer",
                                .argv = {std::string(programs::vncviewer), "-geometry",
                                         std::format("{}x{}+0+0", w, h), "-AlertOnFatalError=0",
                                         "-ReconnectOnError=0",
                                         std::format("{}::{}", bracketed(host.address), host.port)},
                                .env = sup().child_env(),
                                .input = {},
                                .hold_input = false});
        };
        if (!sup().run_xsetup({"--full-screen"}, sized))
            sized(ExitStatus::exited(1), "");
    }

    // The viewer exits with 0 when the user leaves through its F8 menu.
    // Told to stop, it exits with 1, as for errors, but says so first.
    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        if (slot == Slot::client)
            desktop_exited(sup(), status, output, [](int code, std::string_view out) {
                return code == 0 || (code == 1 && out.contains("Termination signal"));
            });
    }
};

// RDP: the probe checks that the host accepts connections, asks for the
// password, and has FreeRDP check the login (+auth-only), so a wrong
// password is reported in the chooser. The password reaches FreeRDP on
// its standard input, never on a command line, and is forgotten once
// the session starts. The host's certificate is trusted the first time
// it is seen (FreeRDP's /cert:tofu), and pixterm-helper keeps it on the
// settings partition, so a changed certificate is refused after a
// restart too.
class RdpSession final : public Session {
public:
    using Session::Session;
    ~RdpSession() override { forget_password(); }
    RdpSession(const RdpSession&) = delete;
    RdpSession& operator=(const RdpSession&) = delete;
    RdpSession(RdpSession&&) = delete;
    RdpSession& operator=(RdpSession&&) = delete;

    bool begin(std::string_view username) override;

    void cancel() override {
        sup().log(std::format("connection to {} cancelled", sup().host().name));
        sup().system().cancel_job();
        back_to_chooser();
    }

    void start() override {
        auto argv = args();
        argv.emplace_back("/f");
        Program p{.name = "xfreerdp",
                  .argv = std::move(argv),
                  .env = sup().child_env(),
                  .input = password_ + '\n',
                  .hold_input = false};
        forget_password();
        run_desktop(sup(), std::move(p));
    }

    // FreeRDP's ordinary ends: success (0), disconnected (1), logged off
    // (2), idle too long (3), disconnected by the user (11), and stopped
    // (145), which is how it ends when told to.
    void child_exited(Slot slot, ExitStatus status, std::string_view output) override {
        if (slot == Slot::client)
            desktop_exited(sup(), status, output, [](int code, std::string_view) {
                return code == 0 || code == 1 || code == 2 || code == 3 || code == 11
                       || code == 145;
            });
    }

    void stop() override { forget_password(); }
    void cleanup() override { forget_password(); }

private:
    std::string user_;
    std::string password_;

    void forget_password() {
        wipe(password_);
        password_.clear();
    }

    void back_to_chooser() {
        forget_password();
        sup().set_state(State::local);
        sup().chooser_idle();
    }

    // FreeRDP's arguments, without the mode.
    [[nodiscard]] std::vector<std::string> args() const {
        const auto& h = sup().host();
        std::vector<std::string> a{std::string(programs::xfreerdp),
                                   "/v:" + with_port(h.address, h.port), "/u:" + user_};
        if (!h.domain.empty())
            a.push_back("/d:" + h.domain);
        // The password comes from a pipe, not a terminal.
        // -grab-keyboard: the console's hot key must still reach pixterm.
        a.insert(a.end(),
                 {"/from-stdin:force", "/cert:tofu", "/log-level:ERROR", "-grab-keyboard"});
        return a;
    }

    void check_login();
    void login_checked(ExitStatus status, std::string_view output);

    // FreeRDP's name for the host's certificate, where it looks for it,
    // and where pixterm keeps it.
    [[nodiscard]] std::string cert_name() const {
        return std::format("{}_{}.pem", sup().host().address, sup().host().port);
    }
    [[nodiscard]] std::filesystem::path live_cert() const {
        return sup().environment().runtime_dir / "home/.config/freerdp/server" / cert_name();
    }
    [[nodiscard]] std::filesystem::path saved_cert() const {
        return sup().environment().state_dir / "rdp-certs" / cert_name();
    }
    void place_cert();
    void save_cert();
};

// FreeRDP sees only the certificate pixterm saved for this host, or none.
void RdpSession::place_cert() {
    auto& system = sup().system();
    if (const auto saved = system.read_file(saved_cert())) {
        if (!system.write_file(live_cert(), *saved))
            sup().log(std::format("cannot write {}", live_cert().string()));
    } else {
        system.remove_file(live_cert());
    }
}

// After a login: a certificate FreeRDP accepted for the first time.
void RdpSession::save_cert() {
    auto& system = sup().system();
    const auto now = system.read_file(live_cert());
    if (!now || now->empty() || now == system.read_file(saved_cert()))
        return;
    const auto done = [this](const helper::Reply& reply) {
        if (reply.ok)
            sup().log("RDP certificate saved");
        else
            sup().log("RDP certificate not saved: " + reply.message);
    };
    if (!system.call_helper({.args = {"rdp-cert-save", cert_name()}, .payload = *now}, done))
        sup().log("RDP certificate not saved: the helper is busy");
}

bool RdpSession::begin(std::string_view username) {
    auto name = login_name(sup().host(), username);
    if (!name) {
        sup().report_error("That user name is not valid.");
        return false;
    }
    user_ = std::move(*name);
    forget_password();
    place_cert();
    const bool started = probe_host(sup(), [this](bool reachable) {
        if (!reachable)
            return;
        const bool asking = ask_password(sup(), user_, [this](std::optional<std::string> password) {
            if (!password) {
                back_to_chooser();
                return;
            }
            password_ = std::move(*password);
            check_login();
        });
        if (!asking) {
            sup().report_error("Something else is still in progress; try again.");
            back_to_chooser();
        }
    });
    if (!started) {
        sup().report_error("Something else is still in progress; try again.");
        return false;
    }
    sup().set_state(State::probe);
    return true;
}

void RdpSession::check_login() {
    auto argv = args();
    argv.emplace_back("+auth-only");
    const bool started = sup().system().start_job(
        {.name = "rdp-login",
         .task = Program{.name = "xfreerdp",
                         .argv = std::move(argv),
                         .env = sup().child_env(),
                         .input = password_ + '\n',
                         .hold_input = false},
         .timeout = rdp_login_timeout,
         .secret = true},
        [this](ExitStatus status, std::string_view output) { login_checked(status, output); });
    if (!started) {
        sup().report_error("Something else is still in progress; try again.");
        back_to_chooser();
    }
}

void RdpSession::login_checked(ExitStatus status, std::string_view output) {
    if (sup().state() != State::probe)
        return;
    if (status.succeeded()) {
        save_cert();
        sup().start_handoff();
        return;
    }
    const auto& name = sup().host().name;
    if (output.contains("AUTHENTICATION_FAILED") || output.contains("LOGON_FAILURE")) {
        sup().report_error(
            std::format("Could not log in to {}.\n\nThe user name or password is wrong.", name));
    } else {
        // FreeRDP's last error, without its time stamp.
        std::string_view why;
        if (const auto at = output.rfind("[ERROR]"); at != std::string_view::npos) {
            why = first_line(output.substr(at));
            if (const auto colon = why.find("]: "); colon != std::string_view::npos)
                why.remove_prefix(colon + 3);
        }
        sup().report_error(std::format("Could not log in to {}.{}", name, paragraph(why)));
    }
    back_to_chooser();
}

} // namespace

std::unique_ptr<Session> make_vnc_session(Supervisor& sup) {
    return std::make_unique<VncSession>(sup);
}

std::unique_ptr<Session> make_rdp_session(Supervisor& sup) {
    return std::make_unique<RdpSession>(sup);
}

} // namespace pixterm::supervisor
