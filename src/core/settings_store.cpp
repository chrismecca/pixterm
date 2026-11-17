// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/settings_store.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <format>

namespace pixterm {

namespace {

constexpr std::string_view header_tag = "#pixterm-config";

constexpr auto crc_table = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1U) != 0 ? 0xedb88320U ^ (c >> 1) : c >> 1;
        table.at(i) = c;
    }
    return table;
}();

std::string describe(const std::filesystem::path& path, std::error_code ec) {
    return std::format("{}: {}", path.string(), ec.message());
}

// Flushes a directory, so renames and removals in it survive power loss.
void sync_directory(const std::filesystem::path& dir) {
    const UniqueFd fd(::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd)
        ::fsync(fd.get());
}

} // namespace

std::uint32_t crc32(std::string_view data) noexcept {
    std::uint32_t c = 0xffffffffU;
    for (const char ch : data)
        c = crc_table.at((c ^ static_cast<unsigned char>(ch)) & 0xffU) ^ (c >> 8);
    return ~c;
}

std::string saved_text(const Settings& settings, std::uint64_t generation) {
    const auto body = to_json(settings);
    return std::format("{} generation={} crc32={:08x}\n{}", header_tag, generation, crc32(body),
                       body);
}

std::expected<Settings, std::string> parse_saved_settings(std::string_view text) {
    if (!text.starts_with(header_tag))
        return parse_settings(text);

    // "#pixterm-config generation=N crc32=XXXXXXXX\n"
    const auto newline = text.find('\n');
    const auto header = text.substr(0, newline);
    const auto body = newline == std::string_view::npos ? "" : text.substr(newline + 1);
    constexpr std::string_view gen_key = " generation=";
    constexpr std::string_view crc_key = " crc32=";
    auto rest = header.substr(header_tag.size());
    const auto space = rest.find(' ', 1);
    if (!rest.starts_with(gen_key) || space == std::string_view::npos
        || !rest.substr(space).starts_with(crc_key))
        return std::unexpected("the settings failed their integrity check");
    const auto generation =
        ascii::parse_decimal<std::uint64_t>(rest.substr(gen_key.size(), space - gen_key.size()));
    rest.remove_prefix(space + crc_key.size());
    std::uint32_t crc = 0;
    const auto [end, ec] = std::from_chars(rest.data(), rest.data() + rest.size(), crc, 16);
    if (!generation || ec != std::errc{} || end != rest.data() + rest.size() || rest.size() != 8
        || crc32(body) != crc)
        return std::unexpected("the settings failed their integrity check");

    auto settings = parse_settings(body);
    if (settings)
        settings->generation = *generation;
    return settings;
}

std::expected<Settings, FileError> read_settings_file(const std::filesystem::path& path) {
    const auto fail = [&path](std::string why) {
        return std::unexpected(FileError{.message = std::format("{}: {}", path.string(), why)});
    };
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!fd) {
        const auto ec = last_error();
        return std::unexpected(FileError{.message = describe(path, ec),
                                         .missing = ec == std::errc::no_such_file_or_directory});
    }
    struct stat st{};
    if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode))
        return fail("not a regular file");
    auto text = read_all(fd.get(), max_settings_size);
    if (!text)
        return fail(text.error() == std::errc::file_too_large ? "too large"
                                                              : text.error().message());
    if (text->contains('\0'))
        return fail("contains NUL bytes");
    auto settings = parse_saved_settings(*text);
    if (!settings)
        return fail(settings.error());
    return std::move(*settings);
}

SettingsStore::SettingsStore(std::filesystem::path state_dir, std::filesystem::path factory_file)
    : dir_(std::move(state_dir)), factory_(std::move(factory_file)) {}

LoadedSettings SettingsStore::load() const {
    LoadedSettings result;
    for (const auto& path : {current(), previous(), factory_}) {
        auto settings = read_settings_file(path);
        if (settings) {
            result.settings = std::move(*settings);
            if (!result.problem.empty())
                result.problem += std::format("; using {} instead", path.string());
            return result;
        }
        if (!settings.error().missing && result.problem.empty())
            result.problem = settings.error().message;
    }
    return result;
}

std::expected<void, std::string> SettingsStore::save(const Settings& settings) const {
    const auto text = saved_text(settings, settings.generation + 1);
    const auto tmp = dir_ / "config.json.tmp";

    // 1. Write the whole new generation and flush it.
    {
        const UniqueFd fd(::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
        if (!fd)
            return std::unexpected(describe(tmp, last_error()));
        auto written = write_all(fd.get(), text);
        if (written && ::fsync(fd.get()) != 0)
            written = std::unexpected(last_error());
        if (!written) {
            ::unlink(tmp.c_str());
            return std::unexpected(describe(tmp, written.error()));
        }
    }

    // 2. Keep the current generation as the previous one, then put the
    // new one in its place. A power cut between the two renames leaves
    // only the previous generation, which load() then uses.
    if (::rename(current().c_str(), previous().c_str()) != 0 && errno != ENOENT) {
        const auto ec = last_error();
        ::unlink(tmp.c_str());
        return std::unexpected(describe(current(), ec));
    }
    if (::rename(tmp.c_str(), current().c_str()) != 0)
        return std::unexpected(describe(current(), last_error()));

    // 3. Flush the directory entries.
    sync_directory(dir_);
    return {};
}

std::expected<void, std::string> SettingsStore::reset() const {
    for (const auto& path : {current(), previous()}) {
        if (::unlink(path.c_str()) != 0 && errno != ENOENT)
            return std::unexpected(describe(path, last_error()));
    }
    sync_directory(dir_);
    return {};
}

} // namespace pixterm
