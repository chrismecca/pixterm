# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The LLVM toolchain: Clang, libc++ and, on Linux, the lld linker.
#
# macOS already uses libc++ and its own linker, so only the compiler is
# chosen there.

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "-stdlib=libc++ -fuse-ld=lld")
endif()

# Homebrew's OpenSSL, which CMake does not find by itself.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" AND EXISTS /opt/homebrew/opt/openssl@3)
    set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
