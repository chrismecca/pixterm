# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_executable(pixterm-helper main.cpp)
target_link_libraries(pixterm-helper PRIVATE
    pixterm_helper_linux pixterm::build_config pixterm::options)
install(TARGETS pixterm-helper DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/pixterm)
