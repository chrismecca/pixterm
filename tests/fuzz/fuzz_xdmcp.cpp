// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// XDMCP answers are UDP datagrams anyone on the network can send.

#include <pixterm/xdmcp.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto reply = pixterm::xdmcp::parse_reply({data, size});
    if (reply
        && (reply->hostname.size() > pixterm::xdmcp::max_text
            || reply->status.size() > pixterm::xdmcp::max_text))
        std::abort();
    return 0;
}
