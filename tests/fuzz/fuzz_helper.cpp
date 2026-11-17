// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// The helper runs as root and reads requests from a socket. Any
// request it accepts must format back to the same bytes.

#include <pixterm/helper_protocol.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view bytes(reinterpret_cast<const char*>(data), size);
    if (const auto request = pixterm::helper::parse_request(bytes)) {
        const auto again = pixterm::helper::format_request(*request);
        if (!again || *again != bytes)
            std::abort();
    }
    (void)pixterm::helper::parse_reply(bytes);
    return 0;
}
