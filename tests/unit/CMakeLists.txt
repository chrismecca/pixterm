# SPDX-FileCopyrightText: 2026 Chris Mecca
# SPDX-License-Identifier: MIT

pixterm_add_tests(validate SOURCES validate_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(json SOURCES json_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(settings SOURCES settings_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(settings_store SOURCES settings_store_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(pin SOURCES pin_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(xdmcp SOURCES xdmcp_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(wire SOURCES wire_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(netstat SOURCES netstat_test.cpp LIBRARIES pixterm::core)
pixterm_add_tests(supervisor SOURCES supervisor_test.cpp LIBRARIES pixterm_supervisor)
pixterm_add_tests(sessions SOURCES sessions_test.cpp LIBRARIES pixterm_supervisor)
