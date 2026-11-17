# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The programs. Each main.cpp only wires together code from src/.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(pixterm-helper)
    add_subdirectory(pixterm-rexec)
    add_subdirectory(pixtermd)
endif()

if(PIXTERM_X11)
    add_subdirectory(pixterm-askpass)
    add_subdirectory(pixterm-ui)
    add_subdirectory(pixterm-xsetup)
endif()
