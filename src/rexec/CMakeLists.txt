# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_library(pixterm_rexec STATIC rexec.cpp)
target_include_directories(pixterm_rexec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pixterm_rexec PUBLIC pixterm::core PRIVATE pixterm::options)
