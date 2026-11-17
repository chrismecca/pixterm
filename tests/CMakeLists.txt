# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

add_library(pixterm_test_harness STATIC harness/main.cpp)
target_include_directories(pixterm_test_harness PUBLIC harness)
target_link_libraries(pixterm_test_harness PRIVATE pixterm::options)

# pixterm_add_tests(<suite> SOURCES <file>... LIBRARIES <target>...)
#
# Builds one test program for the suite and gives CTest one test per
# TEST(name) case in its sources, named <suite>.<name>.
function(pixterm_add_tests suite)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
    set(target test_${suite})
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE pixterm_test_harness pixterm::options ${arg_LIBRARIES})

    foreach(source IN LISTS arg_SOURCES)
        file(STRINGS ${source} lines REGEX "^TEST\\([A-Za-z0-9_]+\\)")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${source})
        foreach(line IN LISTS lines)
            string(REGEX REPLACE "^TEST\\(([A-Za-z0-9_]+)\\).*" "\\1" name "${line}")
            add_test(NAME ${suite}.${name} COMMAND ${target} ${name})
            set_tests_properties(${suite}.${name} PROPERTIES TIMEOUT 30)
        endforeach()
    endforeach()
endfunction()

add_subdirectory(unit)
