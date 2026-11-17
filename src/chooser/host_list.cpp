// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "host_list.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace pixterm::chooser {

std::expected<Edit, std::string> add_host(const Settings& settings, Host host) {
    if (settings.hosts.size() >= max_hosts)
        return std::unexpected("No more hosts can be added.");
    Edit e{.settings = settings, .select = settings.hosts.size()};
    host.id = new_host_id();
    e.settings.hosts.push_back(std::move(host));
    return e;
}

Edit replace_host(const Settings& settings, std::size_t index, Host host) {
    Edit e{.settings = settings, .select = index};
    // The id stays: names and addresses may change.
    host.id = settings.hosts.at(index).id;
    e.settings.hosts.at(index) = std::move(host);
    return e;
}

Edit remove_host(const Settings& settings, std::size_t index) {
    Edit e{.settings = settings, .select = index > 0 ? index - 1 : 0};
    e.settings.hosts.erase(std::next(e.settings.hosts.begin(), static_cast<std::ptrdiff_t>(index)));
    return e;
}

std::optional<Edit> move_host(const Settings& settings, std::size_t index, int by) {
    const auto count = settings.hosts.size();
    if (index >= count || (by < 0 && index == 0) || (by > 0 && index + 1 >= count))
        return std::nullopt;
    const std::size_t to = by < 0 ? index - 1 : index + 1;
    Edit e{.settings = settings, .select = to};
    std::swap(e.settings.hosts.at(index), e.settings.hosts.at(to));
    return e;
}

ListButtons list_buttons(std::size_t count, std::optional<std::size_t> selected) {
    if (!selected || *selected >= count)
        return {};
    return {.edit = true, .remove = true, .up = *selected > 0, .down = *selected + 1 < count};
}

bool has_xdmcp_host(const Settings& settings, std::string_view address) {
    return std::ranges::any_of(settings.hosts, [&](const Host& h) {
        return h.protocol == Protocol::xdmcp && h.address == address;
    });
}

} // namespace pixterm::chooser
