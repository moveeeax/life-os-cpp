/**
 * @file Crypto.cpp
 * @brief Bodies of the Mi Fitness protocol crypto.
 */

#include "fitness/xiaomi/Crypto.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <openssl/rand.h>
#include <openssl/sha.h>

#include "fitness/xiaomi/Errors.hpp"
#include "utils/Base64.hpp"

namespace Xiaomi::Crypto {

namespace {

unsigned char byte_at(std::string_view s, std::size_t i) {
    return static_cast<unsigned char>(s[i]);
}

}  // namespace

std::string b64_encode(std::string_view raw) {
    return Utils::Base64::encode(raw);
}

std::string b64_decode(std::string_view encoded) {
    try {
        return Utils::Base64::decode(encoded);
    } catch (const std::runtime_error& e) {
        // Same message, the protocol's error class: the callers catch
        // MiFitnessAuthError, not a bare runtime_error.
        throw MiFitnessAuthError(e.what());
    }
}

std::string rc4(std::string_view key, std::string_view payload) {
    if (key.empty()) {
        throw MiFitnessAuthError("rc4: empty key (missing or malformed ssecurity)");
    }

    std::array<unsigned char, 256> state{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        state[i] = static_cast<unsigned char>(i);
    }
    std::size_t j = 0;
    for (std::size_t i = 0; i < state.size(); ++i) {
        j = (j + state[i] + byte_at(key, i % key.size())) % 256;
        std::swap(state[i], state[j]);
    }

    std::size_t x = 0;
    std::size_t y = 0;
    const auto next_byte = [&state, &x, &y]() -> unsigned char {
        x = (x + 1) % 256;
        y = (y + state[x]) % 256;
        std::swap(state[x], state[y]);
        return state[(state[x] + state[y]) % 256];
    };

    // Upstream discards 1024 bytes of the stream after key setup. Without this
    // the response decrypts to garbage, and no format test would show it: the
    // response simply fails to parse as JSON.
    for (int skipped = 0; skipped < 1024; ++skipped) {
        next_byte();
    }

    std::string out;
    out.reserve(payload.size());
    for (const char c : payload) {
        out.push_back(static_cast<char>(static_cast<unsigned char>(c) ^ next_byte()));
    }
    return out;
}

std::string signed_nonce(std::string_view ssecurity_b64, std::string_view nonce_raw) {
    const std::string ssecurity = b64_decode(ssecurity_b64);
    std::string buffer;
    buffer.reserve(ssecurity.size() + nonce_raw.size());
    buffer.append(ssecurity);
    buffer.append(nonce_raw);

    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest{};
    SHA256(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size(), digest.data());
    return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}

std::string make_nonce(std::int64_t minutes_since_epoch, std::string_view random8) {
    if (random8.size() != 8) {
        throw MiFitnessAuthError("make_nonce: need exactly eight random bytes");
    }
    std::string nonce(random8);
    const auto value = static_cast<std::uint32_t>(minutes_since_epoch);
    for (int shift = 24; shift >= 0; shift -= 8) {
        nonce.push_back(static_cast<char>((value >> static_cast<unsigned int>(shift)) & 0xff));
    }
    return nonce;
}

std::string random_bytes(std::size_t count) {
    std::string out(count, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(count)) != 1) {
        throw MiFitnessAuthError("random_bytes: OpenSSL RAND_bytes failed");
    }
    return out;
}

std::string signature(std::string_view method,
                      std::string_view path,
                      std::string_view data,
                      std::optional<std::string_view> rc4_hash,
                      std::string_view signed_nonce_raw) {
    std::string base;
    base.append(method).append("&").append(path).append("&data=").append(data);
    if (rc4_hash.has_value()) {
        base.append("&rc4_hash__=").append(*rc4_hash);
    }
    base.append("&").append(b64_encode(signed_nonce_raw));

    std::array<unsigned char, SHA_DIGEST_LENGTH> digest{};
    SHA1(reinterpret_cast<const unsigned char*>(base.data()), base.size(), digest.data());
    return b64_encode(std::string(reinterpret_cast<const char*>(digest.data()), digest.size()));
}

}  // namespace Xiaomi::Crypto
