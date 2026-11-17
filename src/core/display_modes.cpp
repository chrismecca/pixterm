// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/display_modes.hpp>

#include <algorithm>
#include <cmath>
#include <format>

namespace pixterm {

unsigned refresh_rate(const Timings& t) noexcept {
    double lines = t.v_total;
    if (t.double_scan)
        lines *= 2;
    if (t.interlace)
        lines /= 2;
    if (t.h_total == 0 || lines == 0)
        return 0;
    return static_cast<unsigned>(
        std::lround(static_cast<double>(t.dot_clock) / (t.h_total * lines)));
}

std::string edid_monitor_name(std::span<const unsigned char> edid) {
    constexpr std::size_t first_block = 54;
    constexpr std::size_t block_size = 18;
    constexpr unsigned char name_tag = 0xfc;
    if (edid.size() < 128)
        return {};
    for (std::size_t b = first_block; b + block_size <= 126; b += block_size) {
        const auto block = edid.subspan(b, block_size);
        if (block[0] != 0 || block[1] != 0 || block[3] != name_tag)
            continue;
        std::string name;
        for (const auto c : block.subspan(5)) {
            if (c == '\n')
                break;
            name.push_back(c >= 0x20 && c < 0x7f ? static_cast<char>(c) : ' ');
        }
        while (!name.empty() && name.back() == ' ')
            name.pop_back();
        return name;
    }
    return {};
}

std::vector<Mode> sorted_modes(const std::vector<Mode>& modes, unsigned long current_id) {
    std::vector<Mode> kept;
    for (const auto& m : modes) {
        auto same = std::ranges::find_if(kept, [&m](const Mode& k) {
            return k.width == m.width && k.height == m.height && k.hz == m.hz;
        });
        if (same == kept.end())
            kept.push_back(m);
        else if (m.id == current_id)
            same->id = m.id;
    }
    std::ranges::stable_sort(kept, [](const Mode& a, const Mode& b) {
        const auto area_a = static_cast<unsigned long>(a.width) * a.height;
        const auto area_b = static_cast<unsigned long>(b.width) * b.height;
        if (area_a != area_b)
            return area_a > area_b;
        if (a.width != b.width)
            return a.width > b.width;
        return a.hz > b.hz;
    });
    return kept;
}

std::optional<std::size_t> find_mode(const Monitor& m, std::string_view saved) {
    const auto x = saved.find('x');
    const auto at = saved.find('@');
    if (x == std::string_view::npos)
        return std::nullopt;
    const auto w = ascii::parse_decimal<unsigned>(saved.substr(0, x));
    const auto h = ascii::parse_decimal<unsigned>(
        saved.substr(x + 1, at == std::string_view::npos ? std::string_view::npos : at - x - 1));
    const auto hz = at == std::string_view::npos
                        ? std::optional<unsigned>(0)
                        : ascii::parse_decimal<unsigned>(saved.substr(at + 1));
    if (!w || !h || !hz)
        return std::nullopt;
    std::optional<std::size_t> same_size;
    for (std::size_t i = 0; i < m.modes.size(); ++i) {
        const auto& mode = m.modes[i];
        if (mode.width != *w || mode.height != *h)
            continue;
        if (*hz == 0 || mode.hz == *hz)
            return i;
        if (!same_size)
            same_size = i;
    }
    return same_size;
}

std::optional<std::size_t> automatic_mode(const Monitor& m) {
    if (m.modes.empty())
        return std::nullopt;
    if (const auto it = std::ranges::find_if(m.modes, &Mode::preferred); it != m.modes.end())
        return static_cast<std::size_t>(it - m.modes.begin());
    if (m.mm_width == 0 || m.mm_height == 0)
        return 0;
    const double want = static_cast<double>(m.mm_width) / static_cast<double>(m.mm_height);
    std::size_t best = 0;
    double best_error = 1e9;
    for (std::size_t i = 0; i < m.modes.size(); ++i) {
        const double shape = static_cast<double>(m.modes[i].width) / m.modes[i].height;
        const double error = std::fabs(shape - want);
        if (error < best_error - 0.01) {
            best_error = error;
            best = i;
        }
    }
    return best;
}

std::string mode_name(const Mode& m) {
    return std::format("{}x{}@{}", m.width, m.height, m.hz);
}

std::string mode_label(const Mode& m) {
    return std::format("{} x {}   {} Hz", m.width, m.height, m.hz);
}

} // namespace pixterm
