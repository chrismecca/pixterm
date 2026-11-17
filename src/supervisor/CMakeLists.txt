# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# pixtermd's state machine and sessions. The library talks to the
# operating system only through System, so the unit tests can drive it
# on any platform.
add_library(pixterm_supervisor STATIC
    settings_requests.cpp
    supervisor.cpp
    sessions/desktops.cpp
    sessions/direct_x.cpp
    sessions/make_session.cpp
    sessions/parts.cpp
    sessions/remote_exec.cpp
    sessions/serial.cpp
    sessions/ssh.cpp
    sessions/telnet.cpp
    sessions/xdmcp.cpp
)
target_include_directories(pixterm_supervisor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pixterm_supervisor PUBLIC pixterm::core PRIVATE pixterm::options)
