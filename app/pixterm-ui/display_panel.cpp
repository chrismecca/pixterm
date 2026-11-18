// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Configure > Display: the monitor's modes, and when the screen blanks.
// A new mode is tried at once and kept only if the user confirms it in
// time, so a mode the monitor cannot show undoes itself.

#include "ui.hpp"

#include <display.hpp>
#include <panels.hpp>

#include <Xm/Form.h>
#include <Xm/MessageB.h>

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::ui {

namespace {

class DisplayPanel final : public Panel {
public:
    DisplayPanel(App& app, Configure& configure) : app_(app), configure_(configure) {}
    ~DisplayPanel() override { stop_timer(); }
    DisplayPanel(const DisplayPanel&) = delete;
    DisplayPanel& operator=(const DisplayPanel&) = delete;
    DisplayPanel(DisplayPanel&&) = delete;
    DisplayPanel& operator=(DisplayPanel&&) = delete;

    [[nodiscard]] std::string_view title() const override { return "Display"; }
    Widget build(Widget parent) override;
    void refresh() override;

private:
    App& app_;
    Configure& configure_;
    Widget monitor_label_ = nullptr;
    Widget current_label_ = nullptr;
    Widget modes_ = nullptr;
    Widget question_ = nullptr;
    OptionMenu blank_menu_;
    std::optional<Monitor> monitor_;
    std::optional<std::size_t> previous_;
    std::string setting_;
    int seconds_left_ = 0;
    XtIntervalId timer_ = 0;

    void apply();
    void save(const std::string& setting);
    void save_blank(std::size_t choice);
    void try_mode(std::size_t mode);
    void ask();
    void tick();
    void keep();
    void revert();
    void stop_timer();
    void recentre();
};

Widget DisplayPanel::build(Widget parent) {
    Widget form = XtVaCreateWidget("displayPanel", xmFormWidgetClass, parent, XmNhorizontalSpacing,
                                   10, XmNverticalSpacing, 8, nullptr);
    monitor_label_ = make_label(form, "monitor", "Monitor:");
    XtVaSetValues(monitor_label_, XmNtopAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, nullptr);
    current_label_ = make_label(form, "current", "Current mode:");
    XtVaSetValues(current_label_, XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, monitor_label_,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    Widget apply_button = make_button(form, "apply", "Apply...", [this] { apply(); });
    XtVaSetValues(apply_button, XmNbottomAttachment, XmATTACH_FORM, XmNrightAttachment,
                  XmATTACH_FORM, nullptr);
    Widget note =
        make_label(form, "note",
                   std::format("Only modes reported by the monitor are listed. A new mode is\n"
                               "kept only if you confirm it within {} seconds.",
                               chooser::mode_trial_seconds));
    XtVaSetValues(note, XmNbottomAttachment, XmATTACH_FORM, XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_WIDGET, XmNrightWidget, apply_button, nullptr);
    Widget blank_row = XtVaCreateManagedWidget(
        "blankRow", xmFormWidgetClass, form, XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget,
        apply_button, XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    std::vector<std::string> labels;
    labels.reserve(chooser::blank_choices.size());
    for (const int minutes : chooser::blank_choices)
        labels.push_back(chooser::blank_label(minutes));
    const std::vector<std::string_view> views(labels.begin(), labels.end());
    blank_menu_ =
        make_option_menu(blank_row, "blank", views, [this](std::size_t i) { save_blank(i); });
    place_row(blank_row, nullptr, "Blank the screen after:", blank_menu_.menu);
    modes_ = make_list(form, "modeList");
    XtVaSetValues(XtParent(modes_), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, current_label_,
                  XmNbottomAttachment, XmATTACH_WIDGET, XmNbottomWidget, blank_row,
                  XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM, nullptr);
    on(modes_, XmNdefaultActionCallback, [this] { apply(); });
    return form;
}

void DisplayPanel::refresh() {
    blank_menu_.show(chooser::blank_row(app_.settings().blank_minutes));
    monitor_ = x11::query_monitor(XtDisplay(app_.shell));
    set_label(monitor_label_, chooser::monitor_text(monitor_));
    if (!monitor_) {
        set_label(current_label_, "");
        set_items(modes_, {});
        return;
    }
    set_label(current_label_, chooser::current_mode_text(*monitor_));
    set_items(modes_, chooser::mode_rows(*monitor_));
    select_row(modes_, chooser::mode_row(*monitor_, app_.settings().display_mode));
}

void DisplayPanel::save(const std::string& setting) {
    Settings next = app_.settings();
    next.display_mode = setting;
    configure_.commit(next);
}

void DisplayPanel::save_blank(std::size_t choice) {
    const int minutes = chooser::blank_choices.at(choice);
    if (minutes == app_.settings().blank_minutes)
        return;
    Settings next = app_.settings();
    next.blank_minutes = minutes;
    configure_.commit(next);
}

void DisplayPanel::apply() {
    const auto row = selected_row(modes_);
    if (!row || !monitor_ || monitor_->modes.empty())
        return;
    const auto pick = chooser::pick_mode(*monitor_, *row);
    if (!pick.mode || pick.mode == monitor_->current) {
        // Nothing to try out: only remember the choice.
        save(pick.setting);
        return;
    }
    setting_ = pick.setting;
    try_mode(*pick.mode);
}

void DisplayPanel::try_mode(std::size_t mode) {
    if (!monitor_)
        return;
    previous_ = monitor_->current;
    if (!x11::set_mode(XtDisplay(app_.shell), *monitor_, mode)) {
        show_error(configure_.dialog(), "The display could not be switched to that mode.");
        refresh();
        return;
    }
    recentre();
    seconds_left_ = chooser::mode_trial_seconds;
    ask();
}

void DisplayPanel::ask() {
    if (question_ == nullptr) {
        Arg args[2];
        XtSetArg(args[0], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL);
        XtSetArg(args[1], XmNnoResize, True);
        std::string name("modeDialog");
        question_ = XmCreateQuestionDialog(configure_.dialog(), name.data(), args, 2);
        XtUnmanageChild(XmMessageBoxGetChild(question_, XmDIALOG_HELP_BUTTON));
        const CompoundString keep_label("Keep");
        const CompoundString revert_label("Revert");
        XtVaSetValues(question_, XmNokLabelString, keep_label.get(), XmNcancelLabelString,
                      revert_label.get(), XmNdefaultButtonType, XmDIALOG_CANCEL_BUTTON, nullptr);
        XtVaSetValues(XtParent(question_), XmNtitle, "pixterm", nullptr);
        allow_move_only(XtParent(question_));
        on(question_, XmNokCallback, [this] { keep(); });
        on(question_, XmNcancelCallback, [this] { revert(); });
    }
    const CompoundString text(chooser::keep_mode_question(seconds_left_));
    XtVaSetValues(question_, XmNmessageString, text.get(), nullptr);
    XtManageChild(question_);
    timer_ = XtAppAddTimeOut(
        app_.context, 1000,
        [](XtPointer data, XtIntervalId* /*id*/) { static_cast<DisplayPanel*>(data)->tick(); },
        this);
}

void DisplayPanel::tick() {
    timer_ = 0;
    if (--seconds_left_ <= 0) {
        revert();
        return;
    }
    ask();
}

void DisplayPanel::keep() {
    stop_timer();
    XtUnmanageChild(question_);
    save(setting_);
}

void DisplayPanel::revert() {
    stop_timer();
    XtUnmanageChild(question_);
    if (monitor_ && previous_ && x11::set_mode(XtDisplay(app_.shell), *monitor_, *previous_))
        recentre();
    refresh();
}

void DisplayPanel::stop_timer() {
    if (timer_ != 0)
        XtRemoveTimeOut(timer_);
    timer_ = 0;
}

// The screen changed size: keep the chooser and Configure in its middle.
void DisplayPanel::recentre() {
    if (!monitor_ || !monitor_->current)
        return;
    const auto& mode = monitor_->modes.at(*monitor_->current);
    const auto width = static_cast<int>(mode.width);
    const auto height = static_cast<int>(mode.height);
    centre(app_.shell, width, height);
    centre(XtParent(configure_.dialog()), width, height);
    XSync(XtDisplay(app_.shell), False);
}

} // namespace

std::unique_ptr<Panel> make_display_panel(App& app, Configure& configure) {
    return std::make_unique<DisplayPanel>(app, configure);
}

} // namespace pixterm::ui
