# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The code every pixterm program shares: settings, their checks and
# the wire formats. It uses only POSIX, so its tests also run on macOS.
add_library(pixterm_core STATIC
    json.cpp
    validate.cpp
)
add_library(pixterm::core ALIAS pixterm_core)

target_sources(pixterm_core
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/pixterm/json.hpp
            ${PROJECT_SOURCE_DIR}/include/pixterm/validate.hpp
)
target_link_libraries(pixterm_core PRIVATE pixterm::options)
target_compile_features(pixterm_core PUBLIC cxx_std_23)
