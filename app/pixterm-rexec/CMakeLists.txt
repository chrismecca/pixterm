# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_executable(pixterm-rexec main.cpp)
target_link_libraries(pixterm-rexec PRIVATE pixterm_rexec pixterm::options)
install(TARGETS pixterm-rexec DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/pixterm)
