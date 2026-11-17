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
set(PIXTERM_BUILD_ID "" CACHE STRING "The commit the build came from, shown in Configure")

include(CTest)
include(GNUInstallDirs)
include(PixtermOptions)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
