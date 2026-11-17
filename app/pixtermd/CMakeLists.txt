# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_executable(pixtermd main.cpp)
target_link_libraries(pixtermd PRIVATE
    pixterm::supervisor_linux pixterm::build_config pixterm::options)
install(TARGETS pixtermd DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/pixterm)
