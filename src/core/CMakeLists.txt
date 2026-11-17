# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The code every pixterm program shares: settings, their checks and
# the wire formats, and starting programs. It needs only POSIX and
# OpenSSL's libcrypto, so its tests also run on macOS.
add_library(pixterm_core STATIC
    display_modes.cpp
    fd.cpp
    helper_protocol.cpp
    json.cpp
    lines.cpp
    netstat.cpp
    pin.cpp
    settings.cpp
    settings_store.cpp
    spawn.cpp
    validate.cpp
    xdmcp.cpp
)
add_library(pixterm::core ALIAS pixterm_core)

target_sources(pixterm_core
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/pixterm/display_modes.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/fd.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/helper_protocol.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/json.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/lines.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/netstat.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/pin.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/settings.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/settings_store.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/spawn.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/validate.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/xdmcp.hpp
)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
target_link_libraries(pixterm_core PRIVATE pixterm::options OpenSSL::Crypto)
target_compile_features(pixterm_core PUBLIC cxx_std_23)
