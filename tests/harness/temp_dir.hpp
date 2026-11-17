// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#pragma once

#include <stdlib.h> // mkdtemp on Linux
#include <unistd.h> // mkdtemp on macOS

#include <filesystem>
#include <string>
#include <system_error>

namespace pixterm::test {

/// A fresh empty directory, removed with everything in it when the
/// object is destroyed.
class TempDir {
public:
    TempDir() {
        std::error_code ec;
        std::string pattern =
            (std::filesystem::temp_directory_path(ec) / "pixterm-test-XXXXXX").string();
        if (::mkdtemp(pattern.data()) != nullptr)
            path_ = pattern;
    }
    ~TempDir() {
        std::error_code ec;
        if (!path_.empty())
            std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace pixterm::test
