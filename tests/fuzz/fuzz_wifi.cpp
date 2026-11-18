// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Network names in a Wi-Fi scan come from anyone within radio range.
// The same text is read as a scan reply too.

#include <pixterm/validate.hpp>
#include <pixterm/wifi.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    for (const auto& found : {pixterm::parse_scan_results(text), pixterm::parse_scan_reply(text)}) {
        if (found.size() > pixterm::max_wifi_networks)
            std::abort();
        for (const auto& n : found)
            if (!pixterm::is_valid_ssid(n.ssid) || !(n.wpa2 || n.wpa3))
                std::abort();
        // What the helper sends, the chooser reads back unchanged.
        if (pixterm::parse_scan_reply(pixterm::scan_reply(found)).size() != found.size())
            std::abort();
    }
    return 0;
}
