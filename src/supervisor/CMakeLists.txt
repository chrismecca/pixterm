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

# The System pixtermd runs on.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(pixterm_supervisor_linux STATIC
        linux/event_loop.cpp
        linux/linux_system.cpp
        linux/probes.cpp
        linux/spawn.cpp
    )
    add_library(pixterm::supervisor_linux ALIAS pixterm_supervisor_linux)
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    target_link_libraries(pixterm_supervisor_linux
        PUBLIC pixterm_supervisor
        PRIVATE pixterm::options OpenSSL::Crypto)
endif()
