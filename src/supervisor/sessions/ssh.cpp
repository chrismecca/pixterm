// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// SSH hosts. A ControlMaster connection logs in while the chooser and
// mwm are still up, so mwm manages the password and host key dialogs.
// OpenSSH makes the ControlPath socket only once the login succeeds,
// which is the "logged in" signal. After the handoff, the session
// command runs over the master connection with trusted X11 forwarding,
// or, for a terminal host, an interactive login runs in a terminal.
//
// The session ends when the session command exits (a logout) or the
// master connection dies (the network failed). Stray remote X programs
// cannot keep it alive, because teardown kills the master.
//
// A host marked pseudocolor gets an 8-bit display, Xephyr inside the
// terminal's own, and its programs open their windows there. The
// master must already point at it: with ControlMaster, it is the
// master's DISPLAY that ssh forwards to.

#include "parts.hpp"
#include "sessions.hpp"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <format>
#include <iterator>

namespace pixterm::supervisor {

namespace {

constexpr Duration control_poll{200};
constexpr std::string_view terminal_key = "id_ed25519";

enum class Kind : std::uint8_t { master, x11, terminal };

class SshSession final : public Session {
public:
    using Session::Session;

    bool begin(std::string_view username) override;
    void cancel() override;
    void start() override;
    [[nodiscard]] std::vector<std::string> xorg_args() const override {
        return DirectX::xorg_args();
    }
    void display_ready() override {
        direct_x_.allow(sup(), [this] { start_terminal(); });
    }
    void child_exited(Slot slot, ExitStatus status, std::string_view output) override;
    [[nodiscard]] std::optional<Time> deadline() const override {
        return sup().state() == State::connecting ? poll_at_ : nested_.deadline();
    }
    void tick(Time now) override;
    void stop() override { sup().system().close_input(Slot::client); }
    void cleanup() override { sup().system().remove_file(control_path()); }

private:
    std::string user_;
    bool cancelled_ = false;
    std::optional<Time> poll_at_;
    DirectX direct_x_;
    TerminalWindow terminal_;
    NestedDisplay nested_;

    [[nodiscard]] const Host& host() const { return sup().host(); }
    [[nodiscard]] std::filesystem::path control_path() const {
        return sup().environment().runtime_dir / "ssh-control";
    }
    // ssh records accepted host keys in a copy of known_hosts in the
    // runtime directory, since the settings partition is read-only; the
    // helper saves the copy after a login if it changed.
    [[nodiscard]] std::filesystem::path saved_known_hosts() const {
        return sup().environment().state_dir / "known_hosts";
    }
    [[nodiscard]] std::filesystem::path known_hosts() const {
        return sup().environment().runtime_dir / "known_hosts";
    }
    [[nodiscard]] std::vector<std::string> env() const {
        std::vector<std::string> extra{"SSH_ASKPASS=" + sup().libexec("pixterm-askpass"),
                                       "SSH_ASKPASS_REQUIRE=force",
                                       "PIXTERM_HOST_NAME=" + host().name};
        if (host().pseudocolor)
            extra.push_back(NestedDisplay::variable());
        return sup().child_env(std::move(extra));
    }
    [[nodiscard]] std::vector<std::string> args(Kind kind) const;
    void start_session();
    void start_terminal();
    void save_known_hosts();
    [[nodiscard]] bool connect();
    void master_exited(ExitStatus status, std::string_view output);
    void client_exited(ExitStatus status, std::string_view output);
    void nested_exited();
};

std::vector<std::string> SshSession::args(Kind kind) const {
    std::vector<std::string> a{std::string(programs::ssh), "-F", "/dev/null"};
    const auto option = [&a](std::string o) {
        a.emplace_back("-o");
        a.push_back(std::move(o));
    };
    option("ControlPath=" + control_path().string());
    option("UserKnownHostsFile=" + known_hosts().string());
    option("StrictHostKeyChecking=ask");
    option("UpdateHostKeys=no");
    option("HashKnownHosts=no");
    option("VerifyHostKeyDNS=no");
    option("ForwardAgent=no");
    if (kind == Kind::terminal) {
        option("ForwardX11=no");
    } else {
        option("ForwardX11=yes");
        option("ForwardX11Trusted=yes");
        option("XAuthLocation=" + std::string(programs::xauth));
    }
    option("GSSAPIAuthentication=no");
    option("ConnectTimeout=15");
    option("ServerAliveInterval=15");
    option("ServerAliveCountMax=4");
    option("NumberOfPasswordPrompts=3");
    if (host().auth == Auth::key) {
        // The terminal's own key, and nothing else on the system.
        option("IdentityFile=" + (sup().environment().state_dir / terminal_key).string());
        option("IdentitiesOnly=yes");
        option("PubkeyAuthentication=yes");
    } else {
        option("PubkeyAuthentication=no");
    }
    option("LogLevel=ERROR");
    if (kind == Kind::master) {
        option("ControlMaster=yes");
        option("BatchMode=no");
        a.emplace_back("-N");
    } else {
        // Never fall back to a new connection that would prompt.
        option("ControlMaster=no");
        option("BatchMode=yes");
    }
    if (kind == Kind::x11) {
        a.emplace_back("-T");
    } else if (kind == Kind::terminal) {
        // A terminal with no escape character: no way to local commands.
        a.insert(a.end(), {"-tt", "-e", "none"});
    }
    a.insert(a.end(), {"-p", std::to_string(host().port), "-l", user_, "--", host().address});
    return a;
}

bool SshSession::begin(std::string_view username) {
    auto name = login_name(host(), username);
    if (!name) {
        sup().report_error("That user name is not valid.");
        return false;
    }
    user_ = std::move(*name);
    auto& system = sup().system();
    system.remove_file(control_path());
    if (!system.write_file(known_hosts(), system.read_file(saved_known_hosts()).value_or("")))
        sup().log(std::format("cannot write {}", known_hosts().string()));
    if (host().auth == Auth::key
        && !system.read_file(sup().environment().state_dir / terminal_key)) {
        sup().report_error(std::format(
            "{} is set to log in with this terminal's key, but the terminal has no key yet.\n\n"
            "Make one in Configure > Hosts > Terminal Key.",
            host().name));
        return false;
    }
    cancelled_ = false;
    // A host that sleeps is woken, and ssh starts once it answers.
    if (!host().wake_mac.empty()) {
        if (!probe_host(sup(), [this](bool reachable) {
                if (reachable && !connect())
                    sup().chooser_idle();
            })) {
            sup().report_error("Something else is still in progress; try again.");
            return false;
        }
        sup().set_state(State::probe);
        return true;
    }
    return connect();
}

bool SshSession::connect() {
    auto& system = sup().system();
    sup().log(std::format("connecting to {} ({}@{}:{})", host().name, user_, host().address,
                          host().port));
    if (!sup().start(Slot::master, {.name = "ssh",
                                    .argv = args(Kind::master),
                                    .env = env(),
                                    .input = {},
                                    .hold_input = false})) {
        sup().report_error("Internal error: cannot start ssh.");
        return false;
    }
    sup().set_state(State::connecting);
    poll_at_ = system.now() + control_poll;
    return true;
}

void SshSession::cancel() {
    sup().log(std::format("connection to {} cancelled", host().name));
    if (sup().state() == State::probe) {
        sup().system().cancel_job();
        sup().set_state(State::local);
        sup().chooser_idle();
        return;
    }
    cancelled_ = true;
    sup().system().signal(Slot::master, SIGTERM);
}

// The control socket appears once the user has logged in.
void SshSession::tick(Time now) {
    if (sup().state() != State::connecting) {
        nested_.tick(sup(), now);
        return;
    }
    if (sup().system().is_socket(control_path())) {
        poll_at_.reset();
        sup().start_handoff();
    } else {
        poll_at_ = now + control_poll;
    }
}

void SshSession::start() {
    if (host().opens_windows_directly()) {
        sup().switch_display(); // then display_ready
        return;
    }
    if (host().protocol == Protocol::ssh_terminal) {
        start_terminal();
        return;
    }
    if (host().pseudocolor) {
        // Xephyr has no cookie of its own. -ac lets in its only clients,
        // ssh's forwarded connections and mwm, which run here; TCP
        // stays off.
        std::vector<std::string> extra{"-ac", "-nolisten", "tcp"};
        std::ranges::move(nested_font_path(host()), std::back_inserter(extra));
        nested_.start(sup(), std::move(extra), [this] { start_session(); });
        return;
    }
    if (!host().font_server.empty()
        && sup().run_xsetup({"--font-server", host().font_server},
                            [this](ExitStatus status, std::string_view) {
                                if (!status.succeeded())
                                    sup().log(std::format("the font server {} could not be added",
                                                          host().font_server));
                                if (sup().state() == State::handoff)
                                    start_session();
                            }))
        return;
    start_session();
}

void SshSession::start_session() {
    auto argv = args(Kind::x11);
    argv.push_back(remote_command(host()));
    // Standard input stays open until teardown: on OpenVMS the command's
    // input is the SSH channel, and end of file there ends it at once.
    if (!sup().start(Slot::client, {.name = "ssh-session",
                                    .argv = std::move(argv),
                                    .env = env(),
                                    .input = {},
                                    .hold_input = true})) {
        sup().report_error("Internal error: cannot start the remote session.");
        sup().teardown();
        return;
    }
    sup().session_running(State::session);
    save_known_hosts();
}

void SshSession::start_terminal() {
    const auto title = host().opens_windows_directly() && !direct_x_.display().empty()
                           ? "X display " + direct_x_.display()
                           : std::string();
    terminal_.open(sup(), args(Kind::terminal), env(), title);
    save_known_hosts();
}

void SshSession::save_known_hosts() {
    auto& system = sup().system();
    const auto now = system.read_file(known_hosts());
    if (!now || now->empty() || now == system.read_file(saved_known_hosts()))
        return;
    const auto done = [this](const helper::Reply& reply) {
        if (reply.ok)
            sup().log("host key saved");
    };
    if (!system.call_helper({.args = {"known-hosts-save"}, .payload = *now}, done))
        sup().log("host key not saved: the helper is busy");
}

void SshSession::child_exited(Slot slot, ExitStatus status, std::string_view output) {
    if (slot == Slot::master)
        master_exited(status, output);
    else if (slot == Slot::nested)
        nested_exited();
    else if (host().protocol == Protocol::ssh_terminal)
        terminal_exited(sup(), status, output);
    else
        client_exited(status, output);
}

void SshSession::master_exited(ExitStatus status, std::string_view output) {
    const auto s = sup().state();
    if (s == State::connecting) {
        sup().set_state(State::local);
        // SIGTERM: cancelled from the chooser or a password dialog.
        if (!cancelled_ && status.signal != SIGTERM)
            sup().report_error(std::format(
                "Could not log in to {}.\n\n{}", host().name,
                output.empty() ? std::string_view("The connection was closed.") : output));
        sup().chooser_idle();
        return;
    }
    if (s == State::handoff || s == State::session) {
        sup().report_error(
            std::format("The connection to {} was lost.{}", host().name, paragraph(output)));
        sup().teardown();
    }
}

void SshSession::nested_exited() {
    if (!nested_.exited(sup()) || sup().state() != State::session)
        return;
    sup().report_error(std::format("The 8-bit display for {} closed.", host().name));
    sup().teardown();
}

void SshSession::client_exited(ExitStatus status, std::string_view output) {
    if (sup().state() != State::session)
        return;
    const auto ran = sup().system().now() - sup().started(Slot::client);
    const bool custom = host().session == SessionType::custom;
    const std::string hint = custom ? "See ~/.pixterm-session-errors on the host." : std::string();
    const std::string text(output);
    const auto& h = host();
    const auto report = [this, status, ran, custom, hint, text, &h] {
        const int code = status.signal != 0 ? -1 : status.code;
        if (code == 126 || code == 127) {
            sup().report_error(std::format("The session on {} could not start.\n\n{}", h.name,
                                           text.empty() ? hint : std::string(first_line(text))));
        } else if (code == 255 || code < 0) {
            sup().report_error(
                std::format("The connection to {} was lost.{}", h.name, paragraph(text)));
        } else if (ran < session_start_grace) {
            // Even a "successful" exit this soon means no session ran:
            // the command failed, or left its programs in the background.
            std::string why = text;
            if (why.empty())
                why = custom ? hint
                             : "The session command must keep running for as long as the "
                               "session lasts.";
            sup().report_error(std::format("The session on {} ended immediately (status {}).\n\n{}",
                                           h.name, code, why));
        } else if (code == 0) {
            sup().log(std::format("session on {} ended", h.name));
        } else {
            sup().log(std::format("session on {} ended with status {}", h.name, code));
        }
        sup().teardown();
    };
    if (status.succeeded())
        report();
    else
        sup().unless_display_reset(report);
}

} // namespace

std::unique_ptr<Session> make_ssh_session(Supervisor& sup) {
    return std::make_unique<SshSession>(sup);
}

} // namespace pixterm::supervisor
