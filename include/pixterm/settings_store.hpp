// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

/// @file
/// Saved settings on the settings partition.
///
/// Each save writes a new generation, config.json, and keeps the one
/// before as config.prev.json. A generation starts with a header line
/// carrying its number and a CRC-32 of the JSON after it, so a save cut
/// short by a power failure is detected and the previous generation is
/// used. With no valid generation, the factory file is used, and with
/// no factory file, the defaults.

#pragma once

#include <pixterm/settings.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace pixterm {

/// The largest settings file read.
inline constexpr std::size_t max_settings_size = 64 * 1024;

/// The CRC-32 used by zlib and Ethernet.
[[nodiscard]] std::uint32_t crc32(std::string_view data) noexcept;

/// The text of a saved generation: the header line, then the JSON.
[[nodiscard]] std::string saved_text(const Settings& settings, std::uint64_t generation);

/// Reads settings with or without a generation header. Plain JSON, as
/// in factory and exported files, gets generation 0.
[[nodiscard]] std::expected<Settings, std::string> parse_saved_settings(std::string_view text);

/// Why a settings file could not be read.
struct FileError {
    std::string message;
    bool missing = false; ///< the file does not exist
};

/// Reads and checks one settings file: a regular file of at most
/// max_settings_size bytes, with no NUL bytes, that parses.
[[nodiscard]] std::expected<Settings, FileError>
read_settings_file(const std::filesystem::path& path);

/// The result of loading the current settings.
struct LoadedSettings {
    Settings settings;
    /// Why newer settings were passed over, or empty. When nothing
    /// valid was found, settings holds the defaults.
    std::string problem;
};

/// The saved generations in one directory, with a factory file to fall
/// back on.
class SettingsStore {
public:
    SettingsStore(std::filesystem::path state_dir, std::filesystem::path factory_file);

    /// The newest valid settings: the current generation, else the
    /// previous one, else the factory file, else the defaults.
    [[nodiscard]] LoadedSettings load() const;

    /// Saves settings as generation settings.generation + 1. The new
    /// file is complete and flushed before it replaces the current one.
    [[nodiscard]] std::expected<void, std::string> save(const Settings& settings) const;

    /// Removes the saved generations, returning to the factory file.
    [[nodiscard]] std::expected<void, std::string> reset() const;

private:
    std::filesystem::path dir_;
    std::filesystem::path factory_;

    [[nodiscard]] std::filesystem::path current() const { return dir_ / "config.json"; }
    [[nodiscard]] std::filesystem::path previous() const { return dir_ / "config.prev.json"; }
};

} // namespace pixterm
