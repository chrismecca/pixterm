# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.28)

project(pixterm
    VERSION 0.1.0
    DESCRIPTION "An X terminal for the Raspberry Pi"
    HOMEPAGE_URL "https://github.com/chrismecca/pixterm"
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(PIXTERM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PIXTERM_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PIXTERM_FUZZ "Build the libFuzzer targets" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(linux ON)
else()
    set(linux OFF)
endif()
option(PIXTERM_X11 "Build the programs that draw on the screen (X11, Motif)" ${linux})
option(PIXTERM_SYSTEM_FILES "Install the systemd units and the Xorg configuration" ${linux})
set(PIXTERM_BUILD_ID "" CACHE STRING "The commit the build came from, shown in Configure")
set(PIXTERM_XORG "/usr/bin/Xorg" CACHE FILEPATH "The X server pixtermd starts; tests use a stand-in")

include(CTest)
include(GNUInstallDirs)
include(PixtermOptions)

set(PIXTERM_LIBEXEC_DIR "${CMAKE_INSTALL_FULL_LIBEXECDIR}/pixterm")
set(PIXTERM_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/pixterm")
set(PIXTERM_FACTORY_SETTINGS "${CMAKE_INSTALL_FULL_SYSCONFDIR}/pixterm/config.json")
set(PIXTERM_STATE_DIR "${CMAKE_INSTALL_FULL_LOCALSTATEDIR}/lib/pixterm")
configure_file(cmake/build_config.hpp.in include/pixterm/build_config.hpp @ONLY)
add_library(pixterm_build_config INTERFACE)
add_library(pixterm::build_config ALIAS pixterm_build_config)
target_include_directories(pixterm_build_config INTERFACE "${PROJECT_BINARY_DIR}/include")

if(PIXTERM_X11)
    find_package(X11 REQUIRED COMPONENTS Xrandr Xt)
    # Motif has no CMake or pkg-config file on most systems.
    find_library(MOTIF_LIBRARY Xm REQUIRED)
    find_path(MOTIF_INCLUDE_DIR Xm/Xm.h REQUIRED)
    add_library(Motif::Xm UNKNOWN IMPORTED)
    set_target_properties(Motif::Xm PROPERTIES
        IMPORTED_LOCATION "${MOTIF_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MOTIF_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "X11::Xt;X11::X11")
endif()

add_subdirectory(src)
add_subdirectory(app)
add_subdirectory(data)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(PIXTERM_FUZZ)
    add_subdirectory(tests/fuzz)
endif()
