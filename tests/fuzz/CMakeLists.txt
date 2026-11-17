# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

# One libFuzzer program for each parser of untrusted input. Each has a
# seed corpus in corpus/<name>/.
function(pixterm_add_fuzzer name)
    add_executable(fuzz_${name} fuzz_${name}.cpp)
    target_link_libraries(fuzz_${name} PRIVATE pixterm::core pixterm::options)
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
    # Debian builds libFuzzer itself against libstdc++. Its symbols live
    # in a different namespace from libc++'s, so both can be linked into
    # these test programs. The file is named, because with -stdlib=libc++
    # clang turns -lstdc++ into -lc++.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(fuzz_${name} PRIVATE
            -Wl,--push-state,--no-as-needed -l:libstdc++.so.6 -Wl,--pop-state)
    endif()
endfunction()

pixterm_add_fuzzer(settings)
