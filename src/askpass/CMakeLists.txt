# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_library(pixterm_askpass STATIC askpass.cpp)
target_include_directories(pixterm_askpass PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pixterm_askpass PUBLIC pixterm::core PRIVATE pixterm::options)
