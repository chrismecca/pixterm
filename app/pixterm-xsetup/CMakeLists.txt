# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_executable(pixterm-xsetup main.cpp)
target_link_libraries(pixterm-xsetup PRIVATE pixterm::x11 X11::Xext pixterm::build_config
                                             pixterm::options)
install(TARGETS pixterm-xsetup DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/pixterm)
