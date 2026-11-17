# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# pixterm::options carries the language level, warnings, hardening and
# sanitizer flags. Every pixterm target links it privately, so none of
# these flags reach code that pixterm does not own.

include_guard(GLOBAL)

add_library(pixterm_options INTERFACE)
add_library(pixterm::options ALIAS pixterm_options)

target_compile_features(pixterm_options INTERFACE cxx_std_23)

target_compile_options(pixterm_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    -Wextra-semi
    -Wunreachable-code
    # Errors are values (std::expected), never exceptions: exceptions
    # cannot cross the C callbacks of Xt, Motif and libc.
    -fno-exceptions
)

if(PIXTERM_WARNINGS_AS_ERRORS)
    target_compile_options(pixterm_options INTERFACE -Werror)
endif()

# Hardening. _FORTIFY_SOURCE needs optimization, so it is left out of
# debug builds. libc++ checks bounds and preconditions in every build.
target_compile_definitions(pixterm_options INTERFACE
    $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=3>
    $<IF:$<CONFIG:Debug>,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>
)
target_compile_options(pixterm_options INTERFACE -fstack-protector-strong)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(pixterm_options INTERFACE _GNU_SOURCE)
    target_compile_options(pixterm_options INTERFACE -fstack-clash-protection)
    target_link_options(pixterm_options INTERFACE
        LINKER:-z,relro
        LINKER:-z,now
        LINKER:-z,noexecstack
        LINKER:--as-needed
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        target_compile_options(pixterm_options INTERFACE -mbranch-protection=standard)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(pixterm_options INTERFACE -fcf-protection=full)
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CheckPIESupported)
check_pie_supported()

if(PIXTERM_SANITIZE)
    target_compile_options(pixterm_options INTERFACE
        -fsanitize=address,undefined
        -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer
    )
    target_link_options(pixterm_options INTERFACE -fsanitize=address,undefined)
endif()

if(PIXTERM_FUZZ)
    target_compile_options(pixterm_options INTERFACE -fsanitize=fuzzer-no-link)
endif()
