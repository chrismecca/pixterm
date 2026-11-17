# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_executable(pixterm-askpass main.cpp)
target_link_libraries(pixterm-askpass PRIVATE pixterm_askpass Motif::Xm pixterm::options)
install(TARGETS pixterm-askpass DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/pixterm)
