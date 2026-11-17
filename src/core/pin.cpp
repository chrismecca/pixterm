// SPDX-FileCopyrightText: 2026 Chris Mecca
// SPDX-License-Identifier: MIT

#include "ascii.hpp"

#include <pixterm/pin.hpp>
#include <pixterm/validate.hpp>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <format>
#include <optional>
#include <vector>

namespace pixterm {

namespace {

constexpr std::size_t salt_length = 16;
constexpr std::size_t hash_length = 32;

std::string to_hex(std::span<const unsigned char> bytes) {
    std::string out;
    for (const auto b : bytes)
        out += std::format("{:02x}", b);
    return out;
}

std::optional<std::vector<unsigned char>> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0)
        return std::nullopt;
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto byte = ascii::parse_hex<unsigned char>(hex.substr(i, 2));
        if (!byte)
            return std::nullopt;
        out.push_back(*byte);
    }
    return out;
}

bool pbkdf2(std::string_view pin, std::span<const unsigned char> salt, unsigned iterations,
            std::span<unsigned char> out) {
    return PKCS5_PBKDF2_HMAC(pin.data(), static_cast<int>(pin.size()), salt.data(),
                             static_cast<int>(salt.size()), static_cast<int>(iterations),
                             EVP_sha256(), static_cast<int>(out.size()), out.data())
           == 1;
}

} // namespace

bool is_valid_pin(std::string_view pin) noexcept {
    return pin.size() >= min_pin_length && pin.size() <= max_pin_length
           && ascii::all_of(pin, ascii::is_digit);
}

std::expected<std::string, std::string> make_pin_hash(std::string_view pin) {
    if (!is_valid_pin(pin))
        return std::unexpected(
            std::format("A PIN is {} to {} digits.", min_pin_length, max_pin_length));
    std::array<unsigned char, salt_length> salt{};
    std::array<unsigned char, hash_length> hash{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1
        || !pbkdf2(pin, salt, pin_iterations, hash))
        return std::unexpected("The PIN could not be hashed.");
    auto stored = std::format("pbkdf2-sha256${}${}${}", pin_iterations, to_hex(salt), to_hex(hash));
    OPENSSL_cleanse(hash.data(), hash.size());
    return stored;
}

bool verify_pin(std::string_view pin, std::string_view stored) {
    if (!is_valid_pin(pin) || !is_valid_pin_hash(stored))
        return false;
    // is_valid_pin_hash has checked the layout: scheme$iterations$salt$hash.
    stored.remove_prefix(stored.find('$') + 1);
    const auto first = stored.find('$');
    const auto second = stored.find('$', first + 1);
    const auto iterations = ascii::parse_decimal<unsigned>(stored.substr(0, first));
    const auto salt = from_hex(stored.substr(first + 1, second - first - 1));
    const auto want = from_hex(stored.substr(second + 1));
    std::array<unsigned char, hash_length> got{};
    if (!iterations || !salt || !want || want->size() != got.size()
        || !pbkdf2(pin, *salt, *iterations, got))
        return false;
    const bool same = CRYPTO_memcmp(got.data(), want->data(), got.size()) == 0;
    OPENSSL_cleanse(got.data(), got.size());
    return same;
}

void wipe(std::span<char> secret) noexcept {
    OPENSSL_cleanse(secret.data(), secret.size());
}

} // namespace pixterm
