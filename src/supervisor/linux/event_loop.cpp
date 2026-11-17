// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "event_loop.hpp"

#include <sys/signalfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <vector>

namespace pixterm::supervisor {

EventLoop::EventLoop(std::initializer_list<int> signals) {
    sigset_t set;
    sigemptyset(&set);
    for (const int s : signals)
        sigaddset(&set, s);
    sigprocmask(SIG_BLOCK, &set, nullptr);
    signal_fd_.reset(signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC));
    watch_signals_ = watch(signal_fd_.get(), POLLIN, [this](short) {
        signalfd_siginfo info{};
        while (::read(signal_fd_.get(), &info, sizeof info) == static_cast<ssize_t>(sizeof info))
            if (on_signal)
                on_signal(static_cast<int>(info.ssi_signo));
    });
}

int EventLoop::watch(int fd, short events, Callback callback) {
    const int id = next_id_++;
    watches_.emplace(id, Watch{.fd = fd, .events = events, .callback = std::move(callback)});
    return id;
}

void EventLoop::unwatch(int id) {
    watches_.erase(id);
}

void EventLoop::set_events(int id, short events) {
    if (const auto it = watches_.find(id); it != watches_.end())
        it->second.events = events;
}

void EventLoop::run_once(std::optional<Time> deadline) {
    std::vector<pollfd> fds;
    std::vector<int> ids;
    for (const auto& [id, w] : watches_) {
        if (w.fd < 0)
            continue;
        fds.push_back({.fd = w.fd, .events = w.events, .revents = 0});
        ids.push_back(id);
    }
    int timeout = -1;
    if (deadline) {
        const auto wait = std::chrono::ceil<Duration>(*deadline - Clock::now());
        timeout = static_cast<int>(std::clamp<Duration::rep>(wait.count(), 0, 60'000));
    }
    if (::poll(fds.data(), fds.size(), timeout) <= 0)
        return;
    for (std::size_t i = 0; i < fds.size(); ++i) {
        if (fds[i].revents == 0)
            continue;
        // An earlier callback may have removed or replaced this watch.
        const auto it = watches_.find(ids[i]);
        if (it == watches_.end() || it->second.fd != fds[i].fd)
            continue;
        auto callback = it->second.callback; // the watch may go away while it runs
        callback(fds[i].revents);
    }
}

} // namespace pixterm::supervisor
