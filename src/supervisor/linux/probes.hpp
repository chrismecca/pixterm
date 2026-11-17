// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Checks of a host, run in a child process because looking up a name
/// can block. Each returns the one line its job reports; see TcpProbe
/// and XdmcpProbe in system.hpp.

#pragma once

#include "../system.hpp"

#include <string>

namespace pixterm::supervisor {

[[nodiscard]] std::string run_probe(const TcpProbe& probe);
[[nodiscard]] std::string run_probe(const XdmcpProbe& probe);

} // namespace pixterm::supervisor
