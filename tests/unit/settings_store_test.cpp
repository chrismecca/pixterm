// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "temp_dir.hpp"
#include "test.hpp"

#include <pixterm/settings_store.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace pixterm;
using pixterm::test::TempDir;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& path, std::string_view text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

Settings with_layout(std::string layout) {
    Settings s;
    s.keyboard_layout = std::move(layout);
    return s;
}

} // namespace

TEST(nothing_saved_gives_defaults) {
    const TempDir dir;
    const SettingsStore store(dir.path(), dir.path() / "factory.json");
    const auto loaded = store.load();
    CHECK(loaded.settings == Settings{});
    CHECK_EQ(loaded.problem, "");
}

TEST(factory_file_is_used_until_something_is_saved) {
    const TempDir dir;
    write(dir.path() / "factory.json",
          R"({"schema_version":1,"device":{"keyboard":{"layout":"gb"}}})");
    const SettingsStore store(dir.path(), dir.path() / "factory.json");
    CHECK_EQ(store.load().settings.keyboard_layout, "gb");
}

TEST(each_save_is_a_new_generation) {
    const TempDir dir;
    const SettingsStore store(dir.path(), dir.path() / "factory.json");
    REQUIRE(store.save(with_layout("gb")).has_value());
    auto loaded = store.load();
    CHECK_EQ(loaded.settings.generation, 1U);
    CHECK_EQ(loaded.settings.keyboard_layout, "gb");

    loaded.settings.keyboard_layout = "fr";
    REQUIRE(store.save(loaded.settings).has_value());
    loaded = store.load();
    CHECK_EQ(loaded.settings.generation, 2U);
    CHECK_EQ(loaded.settings.keyboard_layout, "fr");
    CHECK(fs::exists(dir.path() / "config.prev.json"));
    CHECK(!fs::exists(dir.path() / "config.json.tmp"));
    CHECK(read(dir.path() / "config.json").starts_with("#pixterm-config generation=2 crc32="));
}

TEST(a_damaged_save_falls_back_to_the_previous_one) {
    const TempDir dir;
    const SettingsStore store(dir.path(), dir.path() / "factory.json");
    REQUIRE(store.save(with_layout("gb")).has_value());
    auto second = with_layout("fr");
    second.generation = 1;
    REQUIRE(store.save(second).has_value());

    auto text = read(dir.path() / "config.json");
    text[text.size() - 5] = '#';
    write(dir.path() / "config.json", text);

    const auto loaded = store.load();
    CHECK_EQ(loaded.settings.generation, 1U);
    CHECK_EQ(loaded.settings.keyboard_layout, "gb");
    CHECK(loaded.problem.contains("integrity"));
}

TEST(nothing_valid_gives_defaults_and_a_problem) {
    const TempDir dir;
    write(dir.path() / "config.json", "{ not json");
    const SettingsStore store(dir.path(), dir.path() / "factory.json");
    const auto loaded = store.load();
    CHECK(loaded.settings == Settings{});
    CHECK(!loaded.problem.empty());
}

TEST(reset_returns_to_the_factory_settings) {
    const TempDir dir;
    const SettingsStore store(dir.path(), dir.path() / "factory.json");
    REQUIRE(store.save(with_layout("gb")).has_value());
    REQUIRE(store.save(with_layout("fr")).has_value());
    REQUIRE(store.reset().has_value());
    CHECK(store.load().settings == Settings{});
}

TEST(saved_text_has_a_checked_header) {
    const auto text = saved_text(with_layout("gb"), 7);
    CHECK(text.starts_with("#pixterm-config generation=7 crc32="));
    const auto back = parse_saved_settings(text);
    REQUIRE(back.has_value());
    CHECK_EQ(back->generation, 7U);
    CHECK_EQ(back->keyboard_layout, "gb");
    CHECK(!parse_saved_settings("#pixterm-config generation=7 crc32=00000000\n{}"));
    CHECK(!parse_saved_settings("#pixterm-config generation=x crc32=00000000\n{}"));
    CHECK(!parse_saved_settings("#pixterm-config\n{}"));
    // Plain JSON, as factory and exported files are, has generation 0.
    CHECK_EQ(parse_saved_settings(R"({"schema_version":1})").value().generation, 0U);
}

TEST(refuses_files_that_are_not_settings) {
    const TempDir dir;
    CHECK(!read_settings_file(dir.path())); // a directory
    CHECK(read_settings_file(dir.path() / "missing").error().missing);
    write(dir.path() / "nul", std::string("{\"schema_version\":1}\0", 21));
    CHECK(!read_settings_file(dir.path() / "nul"));
    write(dir.path() / "big", std::string(max_settings_size + 1, ' '));
    CHECK(!read_settings_file(dir.path() / "big"));
}

TEST(crc32_matches_the_standard) {
    CHECK_EQ(crc32("123456789"), 0xcbf43926U);
    CHECK_EQ(crc32(""), 0U);
}
