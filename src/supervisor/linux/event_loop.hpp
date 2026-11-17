// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// A poll(2) loop: file descriptors with callbacks, and signals read
/// from a signalfd so they arrive as ordinary events.

#pragma once

#include "../system.hpp"

#include <pixterm/fd.hpp>

#include <poll.h>

#include <functional>
#include <initializer_list>
#include <map>
#include <optional>

namespace pixterm::supervisor {

class EventLoop {
public:
    using Callback = std::function<void(short revents)>;

    /// Blocks the given signals and delivers them through on_signal.
    explicit EventLoop(std::initializer_list<int> signals);

    /// Calls callback when fd is ready for events. Returns an id for
    /// unwatch. The loop does not own fd.
    int watch(int fd, short events, Callback callback);
    /// Stops watching. Safe to call from a callback.
    void unwatch(int id);
    /// Changes the events watched for.
    void set_events(int id, short events);

    std::function<void(int signal)> on_signal;

    /// Waits for events until deadline (or forever) and handles them.
    void run_once(std::optional<Time> deadline);

private:
    struct Watch {
        int fd;
        short events;
        Callback callback;
    };
    UniqueFd signal_fd_;
    int watch_signals_ = -1;
    int next_id_ = 0;
    std::map<int, Watch> watches_;
};

} // namespace pixterm::supervisor
