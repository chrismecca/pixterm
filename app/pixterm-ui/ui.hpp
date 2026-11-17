// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// The chooser's windows: the host list (Launcher), the Configure dialog
// and its panels, and Find Hosts. What they decide is in src/chooser;
// here they only draw it and pass on what the user does.

#pragma once

#include "motif.hpp"

#include <pixterm/lines.hpp>
#include <pixterm/settings.hpp>

#include <discovery.hpp>
#include <messages.hpp>
#include <pin_gate.hpp>

#include <Xm/Xm.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::ui {

class Launcher;
class Configure;
class FindHosts;

/// Where the chooser finds its files. pixtermd passes them in the
/// environment.
struct Paths {
    std::filesystem::path state_dir;        ///< saved settings
    std::filesystem::path factory_settings; ///< used when none are saved
    std::filesystem::path runtime_dir;      ///< config.candidate.json, pixtermd.log
};

/// What every window shares: the settings as last saved, the line to
/// pixtermd and the administrator PIN gate.
class App {
public:
    App(XtAppContext context, Widget shell, Paths paths, int control_fd);

    XtAppContext context;
    Widget shell;
    Launcher* launcher = nullptr;
    Configure* configure = nullptr;
    FindHosts* find_hosts = nullptr;
    int exit_status = 0;

    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    [[nodiscard]] const Paths& paths() const noexcept { return paths_; }
    [[nodiscard]] bool connected() const noexcept { return control_fd_ >= 0; }

    /// Reads the saved settings again, saying so if they had a problem.
    void reload_settings();

    void error(std::string_view text) const;

    /// Sends a line to pixtermd, or tells the user it is not there.
    [[nodiscard]] bool send(std::string_view line) const;

    /// Hands new settings to pixtermd, which checks, applies and saves
    /// them, then answers APPLIED or APPLY_FAILED.
    [[nodiscard]] bool apply(const Settings& next) const;

    /// Runs then at once if Configure is open to everyone, else after
    /// the administrator PIN.
    void require_admin(Action then);
    void unlock_admin() noexcept { gate_.unlock(); }

    /// Reads what pixtermd sent; called when the control socket is
    /// readable.
    void read_control();

private:
    Paths paths_;
    int control_fd_;
    LineSplitter lines_;
    Settings settings_;
    chooser::PinGate gate_;
    Widget pin_prompt_ = nullptr;
    askpass::Secret pin_;
    Action after_unlock_;

    void handle(const chooser::Message& message);
    void build_pin_prompt();
    void pin_entered();
};

/// The main window: the host list, Login, Find Hosts and Configure.
class Launcher {
public:
    explicit Launcher(App& app) : app_(app) {}

    void build();
    /// Fills the host list from the settings.
    void show_hosts();
    /// Shows that a session is being started, with a Cancel button.
    void show_connecting(std::string_view name);
    /// The connection attempt is over.
    void idle();
    /// Asks pixtermd for a session with a host found on the network.
    void connect_query(const Host& found);

private:
    App& app_;
    Widget host_list_ = nullptr;
    Widget empty_label_ = nullptr;
    Widget login_ = nullptr;
    Widget find_ = nullptr;
    Widget network_ = nullptr;
    Widget working_ = nullptr;
    Widget user_prompt_ = nullptr;
    bool connecting_ = false;

    [[nodiscard]] const Host* selected_host() const;
    void update_buttons();
    void login();
    void start(const Host& host, std::string_view username);
    void ask_username(const Host& host);
    void username_entered(XmString value);
    void update_network();
};

/// One page of the Configure dialog.
class Panel {
public:
    Panel() = default;
    virtual ~Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
    Panel(Panel&&) = delete;
    Panel& operator=(Panel&&) = delete;

    [[nodiscard]] virtual std::string_view title() const = 0;
    /// Makes the panel's widgets in parent, a form, and returns the
    /// panel's own (unmanaged) form.
    virtual Widget build(Widget parent) = 0;
    /// Shows the current settings. Called whenever the panel is shown
    /// and whenever a change has been saved.
    virtual void refresh() = 0;
};

[[nodiscard]] std::unique_ptr<Panel> make_hosts_panel(App& app, Configure& configure);
[[nodiscard]] std::unique_ptr<Panel> make_display_panel(App& app, Configure& configure);
[[nodiscard]] std::unique_ptr<Panel> make_network_panel(App& app, Configure& configure);
[[nodiscard]] std::unique_ptr<Panel> make_keyboard_panel(App& app, Configure& configure);
[[nodiscard]] std::unique_ptr<Panel> make_system_panel(App& app, Configure& configure);
[[nodiscard]] std::unique_ptr<Panel> make_diagnostics_panel(App& app, Configure& configure);

/// The Configure dialog: a list of panels on the left, the chosen panel
/// on the right.
///
/// Every change is made on a copy of the saved settings and handed to
/// pixtermd. The dialog then refreshes from what was really saved.
class Configure {
public:
    /// Called when a request succeeds, with the text pixtermd sent.
    using Done = std::function<void(const std::string& text)>;

    explicit Configure(App& app);

    void open();
    /// The dialog, for dialogs that open over it. Built on first use.
    [[nodiscard]] Widget dialog();

    /// Saves new settings. done runs if they were saved.
    void commit(const Settings& next, Done done = {});
    /// Sends a request that changes no settings (KEYGEN, RESET, ...).
    void request(chooser::Request request, Done done = {});
    /// Adds a host found on the network.
    void add_host(Host host, Done done);

    /// pixtermd's answer to the request in progress.
    void finished(bool ok, const std::string& text);

private:
    App& app_;
    std::vector<std::unique_ptr<Panel>> panels_;
    std::vector<Widget> panel_widgets_;
    Widget dialog_ = nullptr;
    Widget categories_ = nullptr;
    Widget panel_title_ = nullptr;
    std::size_t shown_ = 0;
    bool pending_ = false;
    Done done_;

    void build();
    void show_panel(std::size_t index);
    void begin(Done done);
};

/// Find Hosts: the XDMCP search.
class FindHosts {
public:
    explicit FindHosts(App& app) : app_(app) {}

    void open();

private:
    App& app_;
    Widget dialog_ = nullptr;
    Widget list_ = nullptr;
    Widget status_ = nullptr;
    Widget query_text_ = nullptr;
    Widget connect_ = nullptr;
    Widget add_ = nullptr;
    std::optional<chooser::XdmcpSearch> search_;
    chooser::FoundHosts found_;
    XtInputId input_ = 0;
    XtIntervalId resend_ = 0;
    XtIntervalId done_ = 0;

    void build();
    void start_search();
    void stop();
    void receive();
    void redraw();
    void update_buttons();
    void query();
    void search_done();
    void connect();
    void add();
    [[nodiscard]] const chooser::Found* selected() const;
};

} // namespace pixterm::ui
