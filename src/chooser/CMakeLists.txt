# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# What the chooser decides, with no X or Motif: its messages to and
# from pixtermd, the host editor's rules, list edits, the PIN gate, the
# Configure panels' forms and the XDMCP search. pixterm-ui draws it.
add_library(pixterm_chooser STATIC
    discovery.cpp
    host_form.cpp
    host_list.cpp
    messages.cpp
    panels.cpp
    pin_gate.cpp
    system_info.cpp
)
target_include_directories(pixterm_chooser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pixterm_chooser PUBLIC pixterm::core PRIVATE pixterm::options)
