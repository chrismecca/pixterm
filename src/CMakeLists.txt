# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_subdirectory(askpass)
add_subdirectory(core)
add_subdirectory(supervisor)
add_subdirectory(helper)
add_subdirectory(rexec)

if(PIXTERM_X11)
    add_subdirectory(x11)
endif()
