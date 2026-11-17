# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# Stand-ins and probes for the session test (tests/integration).
add_executable(helper-call helper_call.cpp)
target_link_libraries(helper-call PRIVATE pixterm::core pixterm::options)

add_executable(fake-rexecd fake_rexecd.cpp)
target_link_libraries(fake-rexecd PRIVATE pixterm::core pixterm::options)
