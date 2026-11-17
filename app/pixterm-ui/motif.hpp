// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Small wrappers over Motif that the chooser's windows share: labels,
// buttons and text fields, lists, option menus, form rows and message
// dialogs. Callbacks are std::function objects owned by their widget.

#pragma once

#include <askpass.hpp>

#include <Xm/Xm.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::ui {

using Action = std::function<void()>;

/// A Motif compound string, freed when destroyed.
class CompoundString {
public:
    explicit CompoundString(std::string_view text);
    ~CompoundString() { XmStringFree(string_); }
    CompoundString(const CompoundString&) = delete;
    CompoundString& operator=(const CompoundString&) = delete;
    CompoundString(CompoundString&&) = delete;
    CompoundString& operator=(CompoundString&&) = delete;

    [[nodiscard]] XmString get() const noexcept { return string_; }

private:
    XmString string_;
};

/// Runs action whenever the named callback of w is called. The action
/// lives as long as the widget.
void on(Widget w, const char* callback, Action action);

void set_label(Widget w, std::string_view text);

Widget make_label(Widget parent, const char* name, std::string_view text);
Widget make_button(Widget parent, const char* name, std::string_view text, Action action = {});
Widget make_text_field(Widget parent, const char* name, short columns);

[[nodiscard]] std::string text_of(Widget field);
void set_text(Widget field, std::string_view text);

[[nodiscard]] bool toggle_state(Widget toggle);
void set_toggle(Widget toggle, bool on);

/// Places "Label:  field" in a form, below above (or at the top if
/// above is null). A stretched field reaches the form's right edge.
/// Returns field.
Widget place_row(Widget form, Widget above, std::string_view label, Widget field,
                 bool stretch = false);

/// An option menu of labels; chosen is told the index picked.
struct OptionMenu {
    Widget menu = nullptr;
    std::vector<Widget> items;

    void show(std::size_t index) const;
};

[[nodiscard]] OptionMenu make_option_menu(Widget parent, const char* name,
                                          std::span<const std::string_view> labels,
                                          const std::function<void(std::size_t)>& chosen);

/// A single-selection list in a scrolled window. Attach the list's
/// parent (the scrolled window) in its form.
Widget make_list(Widget parent, const char* name);
void set_items(Widget list, std::span<const std::string> items);
[[nodiscard]] std::optional<std::size_t> selected_row(Widget list);
/// Selects a row and scrolls to it.
void select_row(Widget list, std::size_t row);

/// Lets mwm move a shell, but not resize, close or iconify it.
void allow_move_only(Widget shell);

/// Centres a shell on a screen of the given size.
void centre(Widget shell, int screen_width, int screen_height);

/// A modal form dialog over parent, with the given title. Unless
/// close_on_button, pressing a button leaves it open.
Widget make_form_dialog(Widget parent, const char* name, std::string_view title,
                        bool close_on_button = true);

/// Message dialogs over parent, destroyed when closed.
void show_error(Widget parent, std::string_view text);
void show_info(Widget parent, std::string_view text);
/// Asks a question; runs yes if the user answers ok_label. Cancel is
/// the default, so Return does not confirm by accident.
void confirm(Widget parent, std::string_view text, std::string_view ok_label, Action yes);

/// Keeps what is typed in a text field in secret and shows asterisks.
void attach_secret(Widget field, askpass::Secret& secret);
/// Empties the field and wipes the secret.
void clear_secret(Widget field, askpass::Secret& secret);

} // namespace pixterm::ui
