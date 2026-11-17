# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

pixterm_add_tests(validate SOURCES validate_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(json SOURCES json_test.cpp LIBRARIES pixterm::core)
