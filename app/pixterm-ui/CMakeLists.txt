# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_executable(pixterm-ui
    app.cpp
    configure.cpp
    diagnostics_panel.cpp
    display_panel.cpp
    find_hosts.cpp
    hosts_panel.cpp
    keyboard_panel.cpp
    launcher.cpp
    main.cpp
    monitor_watch.cpp
    motif.cpp
    network_panel.cpp
    system_panel.cpp
    wifi_panel.cpp
)
target_link_libraries(pixterm-ui PRIVATE
    pixterm_chooser pixterm_askpass pixterm::x11 pixterm::build_config Motif::Xm pixterm::options)
install(TARGETS pixterm-ui DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/pixterm)
