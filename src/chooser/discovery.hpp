// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Find Hosts: the XDMCP search, as on the classic X terminals. A
/// BroadcastQuery goes to every IPv4 broadcast address, and a Query to
/// any host typed in; the display managers that answer are listed.

#pragma once

#include <pixterm/fd.hpp>
#include <pixterm/settings.hpp>
#include <pixterm/xdmcp.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace pixterm::chooser {

/// A display manager that answered.
struct Found {
    std::string address; ///< IPv4, as text
    xdmcp::Reply reply;
};

/// The managers that answered, once each, in the order they first did.
class FoundHosts {
public:
    static constexpr std::size_t limit = 64;

    /// Adds an answer, or updates the earlier one from the same address.
    /// Returns false if the list is full.
    bool add(Found found);
    void clear() noexcept { list_.clear(); }
    [[nodiscard]] const std::vector<Found>& list() const noexcept { return list_; }

private:
    std::vector<Found> list_;
};

/// A row of the Find Hosts list.
[[nodiscard]] std::string found_line(const Found& f);

/// The status line after a search.
[[nodiscard]] std::string answered_text(std::size_t count);

/// A host entry for a found manager, named by its own name if that can
/// be shown, else by its address. It has no id yet.
[[nodiscard]] Host xdmcp_host(const Found& f);

/// A UDP socket that sends XDMCP queries and collects the answers.
class XdmcpSearch {
public:
    /// Opens a non-blocking socket that may broadcast. port is where
    /// managers listen; tests use another.
    [[nodiscard]] static std::expected<XdmcpSearch, std::string> open(std::uint16_t port);

    /// Readable when answers are waiting.
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

    /// Sends a BroadcastQuery to every IPv4 interface's broadcast address.
    void broadcast();

    /// Sends a Query to one host, by name or IPv4 address. Looking up a
    /// name may take a moment.
    ///
    /// @return Nothing, or a sentence for the user.
    [[nodiscard]] std::expected<void, std::string> query(std::string_view host);

    /// Every answer waiting on the socket.
    [[nodiscard]] std::vector<Found> receive();

private:
    XdmcpSearch(UniqueFd fd, std::uint16_t port) : fd_(std::move(fd)), port_(port) {}

    UniqueFd fd_;
    std::uint16_t port_;
};

} // namespace pixterm::chooser
