# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# pixterm-helper's requests, apart from the root-only operations, so
# the unit tests can run them anywhere.
add_library(pixterm_helper STATIC helper.cpp)
target_include_directories(pixterm_helper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pixterm_helper PUBLIC pixterm::core PRIVATE pixterm::options)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(pixterm_helper_linux STATIC linux_privileged.cpp)
    target_link_libraries(pixterm_helper_linux PUBLIC pixterm_helper PRIVATE pixterm::options)
endif()
