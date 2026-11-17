// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "helper.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/pin.hpp>
#include <pixterm/settings_store.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <format>
#include <system_error>

namespace pixterm::helper {

namespace {

constexpr std::string_view networkctl = "/usr/bin/networkctl";
constexpr std::string_view systemctl = "/usr/bin/systemctl";
constexpr std::string_view ssh_keygen = "/usr/bin/ssh-keygen";
constexpr std::string_view key_name = "id_ed25519";
constexpr std::string_view usb_settings = "pixterm-config.json";
constexpr std::string_view usb_known_hosts = "pixterm-known_hosts";
constexpr std::string_view usb_public_key = "pixterm-terminal-key.pub";
constexpr std::size_t max_known_hosts_line = 8192;
constexpr std::size_t max_key = 8192;

using Result = std::expected<std::string, std::string>;

std::unexpected<std::string> fail(std::string why) {
    return std::unexpected(std::move(why));
}

std::string describe(const std::filesystem::path& path, std::error_code ec) {
    return std::format("{}: {}", path.string(), ec.message());
}

void sync_directory(const std::filesystem::path& dir) {
    const UniqueFd fd(::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd)
        ::fsync(fd.get());
}

} // namespace

// ---- pieces ---------------------------------------------------------

std::string network_file(const Network& n) {
    std::string text = "# Written by pixterm-helper from the terminal's settings.\n"
                       "[Match]\nType=ether\n\n";
    if (!n.is_static) {
        // The machine ID changes at every boot (the root is read-only),
        // so the DHCP client identifies itself by its MAC address.
        return text
               + "[Network]\nDHCP=ipv4\nLinkLocalAddressing=no\n\n"
                 "[DHCPv4]\nUseDomains=yes\nClientIdentifier=mac\n";
    }
    text += std::format("[Network]\nAddress={}\nLinkLocalAddressing=no\n", n.address);
    if (!n.gateway.empty())
        text += std::format("Gateway={}\n", n.gateway);
    for (const auto& server : n.dns)
        text += std::format("DNS={}\n", server);
    return text;
}

std::expected<Network, std::string> parse_network_args(std::span<const std::string> args) {
    if (args.size() == 1 && args[0] == "dhcp")
        return Network{};
    if (args.size() != 5 || args[0] != "static")
        return fail("usage: network dhcp | network static ADDRESS GATEWAY DNS1 DNS2");
    Network n{.is_static = true, .address = args[1], .gateway = {}, .dns = {}};
    if (args[2] != "-")
        n.gateway = args[2];
    for (const auto& server : args.subspan(3))
        if (server != "-")
            n.dns.push_back(server);
    if (auto problem = network_problem(n))
        return fail(*problem);
    return n;
}

bool is_valid_known_hosts(std::string_view text) noexcept {
    std::size_t line = 0;
    for (const char c : text) {
        if (c == '\n') {
            line = 0;
            continue;
        }
        if (c < 0x20 || c > 0x7e || ++line > max_known_hosts_line)
            return false;
    }
    return true;
}

std::expected<void, std::string> replace_file(const std::filesystem::path& dir,
                                              std::string_view name, std::string_view data,
                                              unsigned mode) {
    const auto path = dir / name;
    const auto tmp = dir / std::format(".{}.tmp", name);
    ::unlink(tmp.c_str());
    {
        const UniqueFd fd(::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                                 static_cast<mode_t>(mode)));
        if (!fd)
            return fail(describe(tmp, last_error()));
        auto written = write_all(fd.get(), data);
        if (written && ::fsync(fd.get()) != 0)
            written = std::unexpected(last_error());
        if (!written) {
            ::unlink(tmp.c_str());
            return fail(describe(tmp, written.error()));
        }
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        const auto ec = last_error();
        ::unlink(tmp.c_str());
        return fail(describe(path, ec));
    }
    sync_directory(dir);
    return {};
}

std::expected<std::string, ReadError> read_regular_file(const std::filesystem::path& path,
                                                        std::size_t limit) {
    const auto failed = [&path](std::string why, bool missing = false) {
        return std::unexpected(
            ReadError{.missing = missing, .message = std::format("{}: {}", path.string(), why)});
    };
    const UniqueFd fd(::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (!fd)
        return failed(last_error().message(), errno == ENOENT);
    struct stat st{};
    if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode))
        return failed("not a regular file");
    auto text = read_all(fd.get(), limit);
    if (!text)
        return failed(text.error().message());
    return std::move(*text);
}

// ---- requests -------------------------------------------------------

Helper::Helper(Paths paths, Privileged& privileged)
    : paths_(std::move(paths)), privileged_(privileged) {}

template <typename Body>
auto Helper::writing(Body body) -> decltype(body()) {
    if (auto ok = privileged_.begin_writing(); !ok)
        return std::unexpected(ok.error());
    auto result = body();
    privileged_.finish_writing();
    return result;
}

Reply Helper::handle(const Request& request) {
    auto result = dispatch(request);
    if (!result)
        return {.ok = false, .message = std::move(result.error()), .payload = {}};
    return {.ok = true, .message = {}, .payload = std::move(*result)};
}

Result Helper::dispatch(const Request& request) {
    if (request.args.empty())
        return fail("an empty request");
    const auto& command = request.args.front();
    const auto args = std::span(request.args).subspan(1);
    if (command == "network")
        return apply_network(args);
    if (!args.empty())
        return fail(std::format("{} takes no arguments", command));
    if (command == "config-save")
        return save_settings(request.payload);
    if (command == "known-hosts-save")
        return save_known_hosts(request.payload);
    if (command == "config-reset")
        return writing([this] { return reset(); }).transform([] { return std::string(); });
    if (command == "keygen")
        return keygen();
    if (command == "usb-export")
        return usb_export();
    if (command == "usb-import")
        return usb_import();
    if (command == "reboot")
        return reboot();
    return fail("an unknown request");
}

std::expected<void, std::string> Helper::write_network(const Network& network) const {
    std::error_code ec;
    std::filesystem::create_directories(paths_.network_dir, ec);
    if (ec)
        return fail(describe(paths_.network_dir, ec));
    return replace_file(paths_.network_dir, "20-wired.network", network_file(network), 0644);
}

Result Helper::apply_network(std::span<const std::string> args) {
    const auto network = parse_network_args(args);
    if (!network)
        return fail(network.error());
    if (auto ok = write_network(*network); !ok)
        return fail(ok.error());
    if (privileged_.run({std::string(networkctl), "reload"}) != 0)
        return fail("networkctl reload failed");
    for (const auto& link : privileged_.ethernet_interfaces())
        (void)privileged_.run({std::string(networkctl), "reconfigure", link});
    return std::string();
}

std::expected<void, std::string> Helper::store(const Settings& settings) const {
    // The helper, not the caller, decides the generation number.
    const SettingsStore saved(paths_.state_dir, paths_.factory_settings);
    auto next = settings;
    next.generation = saved.load().settings.generation;
    return saved.save(next);
}

Result Helper::save_settings(std::string_view json) {
    const auto settings = parse_settings(json);
    if (!settings)
        return fail(settings.error());
    return writing([&] { return store(*settings); }).transform([] { return std::string(); });
}

Result Helper::save_known_hosts(std::string_view text) {
    if (!is_valid_known_hosts(text))
        return fail("known_hosts holds something other than host keys");
    return writing([&] { return replace_file(paths_.state_dir, "known_hosts", text, 0644); })
        .transform([] { return std::string(); });
}

std::expected<void, std::string> Helper::reset() const {
    if (auto ok = SettingsStore(paths_.state_dir, paths_.factory_settings).reset(); !ok)
        return ok;
    for (const auto& name :
         {std::string("known_hosts"), std::string(key_name), std::string(key_name) + ".pub"}) {
        const auto path = paths_.state_dir / name;
        if (::unlink(path.c_str()) != 0 && errno != ENOENT)
            return fail(describe(path, last_error()));
    }
    sync_directory(paths_.state_dir);
    return {};
}

// The terminal's SSH key: made once by ssh-keygen in a private
// directory, then copied to the settings partition with the private key
// readable only by the pixterm account, which runs ssh.
Result Helper::keygen() {
    const auto private_key = paths_.state_dir / key_name;
    const auto public_key = paths_.state_dir / (std::string(key_name) + ".pub");
    if (::access(private_key.c_str(), F_OK) == 0) {
        if (auto existing = read_regular_file(public_key, max_key))
            return std::move(*existing);
    }

    std::error_code ec;
    std::filesystem::remove_all(paths_.keygen_dir, ec);
    if (::mkdir(paths_.keygen_dir.c_str(), 0700) != 0)
        return fail(describe(paths_.keygen_dir, last_error()));
    const auto cleanup = [this] {
        std::error_code ignored;
        std::filesystem::remove_all(paths_.keygen_dir, ignored);
    };
    std::array<char, 64> host{};
    if (::gethostname(host.data(), host.size() - 1) != 0)
        std::string_view("pixterm").copy(host.data(), host.size() - 1);
    const auto made = paths_.keygen_dir / "key";
    if (privileged_.run({std::string(ssh_keygen), "-q", "-t", "ed25519", "-N", "", "-C",
                         std::format("pixterm@{}", host.data()), "-f", made.string()})
        != 0) {
        cleanup();
        return fail("ssh-keygen failed");
    }
    auto secret = read_regular_file(made, max_key);
    auto shared = read_regular_file(made.string() + ".pub", max_key);
    cleanup();
    if (!secret || !shared)
        return fail("ssh-keygen made no key");

    auto saved = writing([&]() -> std::expected<void, std::string> {
        if (auto ok = replace_file(paths_.state_dir, key_name, *secret, 0600); !ok)
            return ok;
        if (!privileged_.hand_to_service(private_key))
            return fail("the key could not be given to the pixterm account");
        return replace_file(paths_.state_dir, std::string(key_name) + ".pub", *shared, 0644);
    });
    wipe(*secret);
    if (!saved)
        return fail(saved.error());
    return std::move(*shared);
}

// The settings, the saved host keys and the terminal's public key;
// never its private key.
Result Helper::usb_export() {
    const auto settings = SettingsStore(paths_.state_dir, paths_.factory_settings).load();
    const auto known = read_regular_file(paths_.state_dir / "known_hosts", max_payload);
    const auto key =
        read_regular_file(paths_.state_dir / (std::string(key_name) + ".pub"), max_key);
    if (!known && !known.error().missing)
        return fail(known.error().message);
    const auto drive = privileged_.mount_usb();
    if (!drive)
        return fail(drive.error());
    auto ok = replace_file(paths_.usb_dir, usb_settings, to_json(settings.settings), 0600);
    if (ok && known)
        ok = replace_file(paths_.usb_dir, usb_known_hosts, *known, 0600);
    if (ok && key)
        ok = replace_file(paths_.usb_dir, usb_public_key, *key, 0644);
    privileged_.unmount_usb();
    if (!ok)
        return fail(ok.error());
    return std::format("The settings were saved to the USB drive ({}) as {}.\n\n"
                       "You can take the drive out now.",
                       *drive, usb_settings);
}

Result Helper::usb_import() {
    const auto drive = privileged_.mount_usb();
    if (!drive)
        return fail(drive.error());
    const auto text = read_regular_file(paths_.usb_dir / usb_settings, max_settings_size);
    const auto known = read_regular_file(paths_.usb_dir / usb_known_hosts, max_payload);
    privileged_.unmount_usb();
    if (!text && text.error().missing)
        return fail(std::format("The USB drive has no {}.", usb_settings));
    if (!text)
        return fail(text.error().message);
    const auto settings = parse_saved_settings(*text);
    if (!settings)
        return fail(
            std::format("The settings on the USB drive are not valid: {}", settings.error()));
    if (!known && !known.error().missing)
        return fail(known.error().message);
    if (known && !is_valid_known_hosts(*known))
        return fail(std::format("{} on the USB drive is not valid.", usb_known_hosts));
    return writing([&]() -> std::expected<void, std::string> {
               if (auto ok = store(*settings); !ok)
                   return ok;
               if (known)
                   return replace_file(paths_.state_dir, "known_hosts", *known, 0644);
               return {};
           })
        .transform([] { return std::string(); });
}

Result Helper::reboot() {
    ::sync();
    if (privileged_.run({std::string(systemctl), "--no-block", "reboot"}) != 0)
        return fail("the restart could not be started");
    return std::string();
}

std::expected<void, std::string> Helper::boot(bool reset_requested) {
    if (reset_requested) {
        if (auto ok = writing([this] { return reset(); }); !ok)
            return ok;
    }
    return write_network(
        SettingsStore(paths_.state_dir, paths_.factory_settings).load().settings.network);
}

} // namespace pixterm::helper
