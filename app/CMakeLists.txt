# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# The programs. Each main.cpp only wires together code from src/.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(pixterm-helper)
    add_subdirectory(pixtermd)
endif()
