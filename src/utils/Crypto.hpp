/**
 * @file Crypto.hpp
 * @brief HMAC-SHA256 + constant-time compare + random hex.
 *
 * Centralized so Auth (JWT), Tokens (link tokens) and any future primitive
 * share one implementation instead of carrying near-identical copies.
 *
 * Declarations only — the bodies live in Crypto.cpp (compiled once into
 * app_core; docs/ARCHITECTURE.md §4): including this header no longer
 * pulls the OpenSSL headers into the including TU. Only std types survive in
 * the signatures. The one exception is to_hex, which is inline and std-only
 * so the std-only TUs (observability/Trace.cpp, compiled straight into a
 * libFuzzer harness) can use it without linking Crypto.cpp.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace Utils::Crypto {

/// Lowercase hex encoding of @p n bytes. Single source for random_hex /
/// sha256_hex and for the hex-of-digest callers outside utils (Storage's
/// SigV4 signing, Webhooks' HMAC signature header, the trace ids).
inline std::string to_hex(const unsigned char* data, std::size_t n) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

/// Lowercase hex encoding of the bytes of @p bytes.
inline std::string to_hex(std::string_view bytes) {
    return to_hex(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

namespace detail {
/// The old name of to_hex, kept as a forwarder for its callers outside utils.
std::string bytes_to_hex(const unsigned char* data, std::size_t n);
}  // namespace detail

/// Raw (binary) HMAC-SHA256 of @p data under @p key. Throws on OpenSSL failure.
std::string hmac_sha256(std::string_view key, std::string_view data);

/// Length-guarded CRYPTO_memcmp wrapper: timing-safe equality for MACs/tokens.
bool constant_time_equals(std::string_view a, std::string_view b);

/**
 * @brief Random hex string of @p byte_count bytes (so output is byte_count*2 chars).
 *        Fails closed: throws if the CSPRNG is unavailable rather than degrading
 *        to a guessable clock value (this feeds refresh-token JTIs / link tokens,
 *        where a predictable value would weaken the revocation namespace).
 */
std::string random_hex(std::size_t byte_count);

/**
 * @brief Lowercase hex SHA-256 of @p s. Used by the Idempotency middleware
 *        to fingerprint request/response bodies.
 */
std::string sha256_hex(std::string_view s);

}  // namespace Utils::Crypto
