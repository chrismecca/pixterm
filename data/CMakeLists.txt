# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The files installed beside the programs: X resources and the mwm
# configuration, and on Linux the systemd units and Xorg configuration
# of the appliance.
configure_file(Xresources.in Xresources @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Xresources Xresources-large mwmrc
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixterm)

if(NOT PIXTERM_SYSTEM_FILES)
    return()
endif()

set(PIXTERM_SYSTEMD_UNIT_DIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system"
    CACHE PATH "Where the systemd units go")
foreach(unit pixtermd.service pixterm-boot.service pixterm-helper@.service)
    configure_file(systemd/${unit}.in ${unit} @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${unit} DESTINATION ${PIXTERM_SYSTEMD_UNIT_DIR})
endforeach()
install(FILES systemd/pixterm-helper.socket DESTINATION ${PIXTERM_SYSTEMD_UNIT_DIR})

set(xorg_conf_dir "${CMAKE_INSTALL_FULL_SYSCONFDIR}/X11/xorg.conf.d")
install(FILES xorg/10-pixterm.conf DESTINATION ${xorg_conf_dir})
# pixtermd writes the keyboard and display size to /run before each X
# start, so
# the link's target does not exist at install time.
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}${xorg_conf_dir}\")
    file(CREATE_LINK /run/pixterm/xorg-settings.conf
         \"\$ENV{DESTDIR}${xorg_conf_dir}/20-pixterm-settings.conf\" SYMBOLIC)
")
