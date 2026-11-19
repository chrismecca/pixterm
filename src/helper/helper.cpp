// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "helper.hpp"

#include <pixterm/fd.hpp>
#include <pixterm/pin.hpp>
#include <pixterm/settings_store.hpp>
#include <pixterm/validate.hpp>
#include <pixterm/wifi.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <format>
#include <system_error>

namespace pixterm::helper {

namespace {

constexpr std::string_view networkctl = "/usr/bin/networkctl";
constexpr std::string_view systemctl = "/usr/bin/systemctl";
constexpr std::string_view ssh_keygen = "/usr/bin/ssh-keygen";
constexpr std::string_view wpa_cli = "/usr/sbin/wpa_cli";
constexpr std::string_view wifi_secret = "wifi-passphrase";
constexpr std::string_view wifi_conf = "wpa_supplicant.conf";
constexpr std::size_t max_wifi_secret = 256;
constexpr std::string_view rdp_certs = "rdp-certs";
constexpr std::size_t max_pem = std::size_t{16} * 1024;
// A scan of both bands takes a few seconds.
constexpr std::chrono::milliseconds scan_time{4'000};
constexpr std::chrono::milliseconds socket_poll{250};
constexpr int socket_polls = 20;
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

std::string known_hosts_without(std::string_view text, std::string_view host, int port) {
    const auto pattern = port == 22 ? std::string(host) : std::format("[{}]:{}", host, port);
    std::string out;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const auto line =
            text.substr(0, newline == std::string_view::npos ? text.size() : newline + 1);
        text.remove_prefix(line.size());
        // The first field is a comma-separated list of names.
        const auto names = line.substr(0, line.find(' '));
        bool match = false;
        for (std::string_view rest = names; !rest.empty() && !match;) {
            const auto comma = rest.find(',');
            match = rest.substr(0, comma) == pattern;
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
        }
        if (!match)
            out += line;
    }
    return out;
}

bool is_valid_rdp_cert_name(std::string_view name) noexcept {
    constexpr std::string_view suffix = ".pem";
    if (!name.ends_with(suffix))
        return false;
    name.remove_suffix(suffix.size());
    const auto underscore = name.rfind('_');
    if (underscore == std::string_view::npos)
        return false;
    const auto port = name.substr(underscore + 1);
    int number = 0;
    const auto [end, ec] = std::from_chars(port.data(), port.data() + port.size(), number);
    return is_valid_address(name.substr(0, underscore)) && !port.empty() && ec == std::errc{}
           && end == port.data() + port.size() && number >= 1 && number <= 65535
           && port.front() != '0';
}

bool is_valid_pem_certificate(std::string_view text) noexcept {
    constexpr std::string_view begin = "-----BEGIN CERTIFICATE-----\n";
    constexpr std::string_view end = "-----END CERTIFICATE-----\n";
    if (text.size() > max_pem || !text.starts_with(begin) || !text.ends_with(end)
        || text.size() < begin.size() + end.size())
        return false;
    auto body = text.substr(begin.size(), text.size() - begin.size() - end.size());
    if (body.empty())
        return false;
    // Lines of base64, each ending in a newline.
    while (!body.empty()) {
        const auto newline = body.find('\n');
        if (newline == 0 || newline == std::string_view::npos || newline > 76)
            return false;
        const auto base64 = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                   || c == '+' || c == '/' || c == '=';
        };
        if (!std::ranges::all_of(body.substr(0, newline), base64))
            return false;
        body.remove_prefix(newline + 1);
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
    if (command == "wifi")
        return wifi(args, request.payload);
    if (command == "wifi-scan")
        return wifi_scan(args);
    if (command == "rdp-cert-save")
        return save_rdp_cert(args, request.payload);
    if (command == "host-key-forget")
        return forget_host_key(args);
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
    if (command == "usb-diagnostics")
        return usb_diagnostics();
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
    if (auto ok = forget_wifi(); !ok)
        return ok;
    std::error_code ec;
    std::filesystem::remove_all(paths_.state_dir / rdp_certs, ec);
    if (ec)
        return fail(describe(paths_.state_dir / rdp_certs, ec));
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

// For a bug report: what the terminal logged since it started, Xorg's
// log, and the settings without the PIN's hash.
Result Helper::usb_diagnostics() {
    constexpr std::size_t max_log = std::size_t{16} * 1024 * 1024;
    const auto journal =
        privileged_.run_output({"/usr/bin/journalctl", "-b", "--no-pager"}).value_or("");
    const auto xorg = read_regular_file(paths_.xorg_log, max_log);
    auto settings = SettingsStore(paths_.state_dir, paths_.factory_settings).load().settings;
    settings.admin_pin.clear();
    const auto drive = privileged_.mount_usb();
    if (!drive)
        return fail(drive.error());
    const auto dir = paths_.usb_dir / "pixterm-diagnostics";
    std::expected<void, std::string> ok;
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST)
        ok = fail(describe(dir, last_error()));
    if (ok)
        ok = replace_file(dir, "journal.txt", journal.substr(0, max_log), 0600);
    if (ok)
        ok = replace_file(dir, "Xorg.log", xorg ? *xorg : xorg.error().message, 0600);
    if (ok)
        ok = replace_file(dir, "settings.json", to_json(settings), 0600);
    privileged_.unmount_usb();
    if (!ok)
        return fail(ok.error());
    return std::format(
        "The diagnostics were saved to the USB drive ({}) in pixterm-diagnostics.\n\n"
        "They name your hosts; look through them before you send them.",
        *drive);
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
    const auto settings = SettingsStore(paths_.state_dir, paths_.factory_settings).load().settings;
    if (auto ok = write_network(settings.network); !ok)
        return ok;
    // wpa_supplicant starts after this, if its configuration exists.
    if (settings.wifi.enabled) {
        if (auto passphrase = stored_passphrase(settings.wifi.ssid)) {
            auto ok = write_wifi_conf(settings.wifi, *passphrase);
            wipe(*passphrase);
            return ok;
        }
    }
    return {};
}

// ---- Wi-Fi ------------------------------------------------------------
//
// The passphrase is kept on the settings partition, readable by root
// alone, beside the network it is for: "SSIDHEX\nPASSPHRASE\n". The
// configuration wpa_supplicant reads is written to /run from it, at boot
// and whenever it changes.

std::optional<std::string> Helper::stored_passphrase(std::string_view ssid) const {
    auto text = read_regular_file(paths_.state_dir / wifi_secret, max_wifi_secret);
    if (!text)
        return std::nullopt;
    std::optional<std::string> found;
    const auto newline = text->find('\n');
    if (newline != std::string::npos && text->ends_with('\n')
        && std::string_view(*text).substr(0, newline) == to_hex(ssid)) {
        auto passphrase = text->substr(newline + 1, text->size() - newline - 2);
        if (is_valid_wifi_passphrase(passphrase))
            found = std::move(passphrase);
    }
    wipe(*text);
    return found;
}

std::expected<void, std::string> Helper::write_wifi_conf(const Wifi& wifi,
                                                         std::string_view passphrase) const {
    if (::mkdir(paths_.wifi_dir.c_str(), 0700) != 0 && errno != EEXIST)
        return fail(describe(paths_.wifi_dir, last_error()));
    auto text = wpa_supplicant_conf(wifi, passphrase);
    auto ok = replace_file(paths_.wifi_dir, wifi_conf, text, 0600);
    wipe(text);
    return ok;
}

std::expected<void, std::string> Helper::forget_wifi() const {
    for (const auto& path : {paths_.state_dir / wifi_secret, paths_.wifi_dir / wifi_conf}) {
        if (::unlink(path.c_str()) != 0 && errno != ENOENT)
            return fail(describe(path, last_error()));
    }
    if (const auto link = privileged_.wifi_interface())
        (void)privileged_.run(
            {std::string(systemctl), "stop", std::format("wpa_supplicant@{}.service", *link)});
    return {};
}

Result Helper::wifi(std::span<const std::string> args, std::string_view passphrase) {
    if (args.size() == 1 && args[0] == "off")
        return writing([this] { return forget_wifi(); }).transform([] { return std::string(); });
    if (args.size() != 4 || args[0] != "on")
        return fail("usage: wifi on COUNTRY SSIDHEX 0|1 | wifi off");
    auto ssid = from_hex(args[2]);
    if (!is_valid_country(args[1]) || !ssid || !is_valid_ssid(*ssid)
        || (args[3] != "0" && args[3] != "1"))
        return fail("wifi: invalid arguments");
    const Wifi wifi{.enabled = true, .ssid = *ssid, .hidden = args[3] == "1", .country = args[1]};
    const auto link = privileged_.wifi_interface();
    if (!link)
        return fail("This terminal has no Wi-Fi.");

    std::string secret(passphrase);
    if (secret.empty()) {
        auto stored = stored_passphrase(wifi.ssid);
        if (!stored)
            return fail(std::format("Type the passphrase for {}.", wifi.ssid));
        secret = std::move(*stored);
    } else if (!is_valid_wifi_passphrase(secret)) {
        wipe(secret);
        return fail("The passphrase must be 8 to 63 letters, digits, spaces or punctuation.");
    } else {
        auto line = std::format("{}\n{}\n", args[2], secret);
        auto stored =
            writing([&] { return replace_file(paths_.state_dir, wifi_secret, line, 0600); });
        wipe(line);
        if (!stored) {
            wipe(secret);
            return fail(stored.error());
        }
    }
    auto written = write_wifi_conf(wifi, secret);
    wipe(secret);
    if (!written)
        return fail(written.error());
    if (privileged_.run(
            {std::string(systemctl), "restart", std::format("wpa_supplicant@{}.service", *link)})
        != 0)
        return fail("Wi-Fi could not be started.");
    return std::string();
}

// Certificates are public: pixtermd reads them to hand to FreeRDP.
Result Helper::save_rdp_cert(std::span<const std::string> args, std::string_view pem) {
    if (args.size() != 1 || !is_valid_rdp_cert_name(args[0]))
        return fail("usage: rdp-cert-save HOST_PORT.pem");
    if (!is_valid_pem_certificate(pem))
        return fail("That is not a certificate.");
    return writing([&]() -> std::expected<void, std::string> {
               const auto dir = paths_.state_dir / rdp_certs;
               if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
                   return fail(describe(dir, last_error()));
               return replace_file(dir, args[0], pem, 0644);
           })
        .transform([] { return std::string(); });
}

Result Helper::forget_host_key(std::span<const std::string> args) {
    int port = 0;
    if (args.size() == 2) {
        const auto& p = args[1];
        const auto [end, ec] = std::from_chars(p.data(), p.data() + p.size(), port);
        if (ec != std::errc{} || end != p.data() + p.size())
            port = 0;
    }
    if (args.size() != 2 || !is_valid_address(args[0]) || port < 1 || port > 65535)
        return fail("usage: host-key-forget ADDRESS PORT");
    const auto known = read_regular_file(paths_.state_dir / "known_hosts", max_payload);
    if (!known && !known.error().missing)
        return fail(known.error().message);
    const auto kept = known ? known_hosts_without(*known, args[0], port) : std::string();
    const auto cert = paths_.state_dir / rdp_certs / std::format("{}_{}.pem", args[0], port);
    return writing([&]() -> std::expected<void, std::string> {
               if (known && kept != *known)
                   if (auto ok = replace_file(paths_.state_dir, "known_hosts", kept, 0644); !ok)
                       return ok;
               if (::unlink(cert.c_str()) != 0 && errno != ENOENT)
                   return fail(describe(cert, last_error()));
               return {};
           })
        .transform([] { return std::string(); });
}

Result Helper::wifi_scan(std::span<const std::string> args) {
    if (args.size() != 1 || !is_valid_country(args[0]))
        return fail("usage: wifi-scan COUNTRY");
    const auto link = privileged_.wifi_interface();
    if (!link)
        return fail("This terminal has no Wi-Fi.");
    const auto unit = std::format("wpa_supplicant@{}.service", *link);
    const auto cli = [&](std::string_view command) {
        return privileged_.run_output({std::string(wpa_cli), "-i", *link, std::string(command)});
    };

    // Joined or joining, wpa_supplicant already runs; otherwise it runs
    // for the scan alone, with no network to join.
    const bool running = std::filesystem::exists(paths_.wifi_dir / wifi_conf);
    if (!running) {
        if (auto ok = write_wifi_conf(
                {.enabled = false, .ssid = {}, .hidden = false, .country = args[0]}, "");
            !ok)
            return fail(ok.error());
        if (privileged_.run({std::string(systemctl), "start", unit}) != 0)
            return fail("Wi-Fi could not be started.");
    }
    for (int i = 0; i < socket_polls && cli("ping") != "PONG\n"; ++i)
        privileged_.pause(socket_poll);
    std::optional<std::string> results;
    if (cli("scan") == "OK\n") {
        privileged_.pause(scan_time);
        results = cli("scan_results");
    }
    if (!running) {
        (void)privileged_.run({std::string(systemctl), "stop", unit});
        ::unlink((paths_.wifi_dir / wifi_conf).c_str());
    }
    if (!results)
        return fail("The Wi-Fi scan failed.");
    return scan_reply(parse_scan_results(*results));
}

} // namespace pixterm::helper
