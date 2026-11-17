// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "spawn.hpp"

#include <pixterm/fd.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstring>
#include <print>

namespace pixterm::supervisor {

namespace {

// What a child does first: the signals pixtermd blocks or ignores go
// back to normal.
void reset_signals() {
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    for (const int s : {SIGCHLD, SIGTERM, SIGINT, SIGHUP, SIGPIPE})
        (void)std::signal(s, SIG_DFL);
}

// execve's argument lists, pointing into strings, which must outlive
// them.
std::vector<char*> pointers(std::vector<std::string>& strings) {
    std::vector<char*> p;
    p.reserve(strings.size() + 1);
    for (auto& s : strings)
        p.push_back(s.data());
    p.push_back(nullptr);
    return p;
}

} // namespace

std::expected<pid_t, std::error_code> spawn(const SpawnOptions& options) {
    if (options.argv.empty())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    // Everything that allocates happens before fork.
    auto args = options.argv;
    auto vars = options.env;
    const auto argv = pointers(args);
    const auto envp = pointers(vars);
    const UniqueFd null(::open("/dev/null", O_RDWR | O_CLOEXEC));
    if (!null)
        return std::unexpected(last_error());

    const pid_t pid = ::fork();
    if (pid < 0)
        return std::unexpected(last_error());
    if (pid > 0)
        return pid;

    reset_signals();
    if (options.new_session)
        ::setsid();
    const std::array fds{options.in, options.out, options.err};
    for (int i = 0; i < 3; ++i) {
        const int from = fds.at(static_cast<std::size_t>(i));
        if (from != inherit_fd && ::dup2(from < 0 ? null.get() : from, i) < 0)
            ::_exit(127);
    }
    if (options.pass_fd >= 0)
        ::fcntl(options.pass_fd, F_SETFD, 0);
    ::execve(argv[0], argv.data(), envp.data());
    std::println(stderr, "pixtermd: cannot run {}: {}", args.front(), std::strerror(errno));
    ::_exit(127);
}

std::expected<pid_t, std::error_code> fork_task(const std::function<std::string()>& task, int out) {
    const pid_t pid = ::fork();
    if (pid < 0)
        return std::unexpected(last_error());
    if (pid > 0)
        return pid;
    reset_signals();
    ::setsid();
    const auto line = task() + '\n';
    (void)write_all(out, line);
    ::_exit(0);
}

} // namespace pixterm::supervisor
