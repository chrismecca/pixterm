# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# What the programs that draw on the screen share.
add_library(pixterm_x11 STATIC display.cpp)
add_library(pixterm::x11 ALIAS pixterm_x11)
target_include_directories(pixterm_x11 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pixterm_x11 PUBLIC pixterm::core X11::X11 X11::Xrandr PRIVATE pixterm::options)
