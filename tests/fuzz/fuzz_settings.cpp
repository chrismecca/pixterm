// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

// Settings files come from USB drives. Whatever they hold, reading
// them must not crash, and anything accepted must write out and read
// back the same.

#include <pixterm/settings_store.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    const auto settings = pixterm::parse_saved_settings(text);
    if (!settings)
        return 0;
    const auto again =
        pixterm::parse_saved_settings(pixterm::saved_text(*settings, settings->generation));
    if (!again || *again != *settings)
        std::abort();
    return 0;
}
