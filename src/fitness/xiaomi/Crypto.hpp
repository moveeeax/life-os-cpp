/**
 * @file Crypto.hpp
 * @brief Crypto of the closed Mi Fitness cloud protocol: RC4, nonce, signature.
 *
 * Pure functions, no network and no state. Conformance to upstream is checked
 * by golden vectors (tests/fixtures/xiaomi_crypto_vectors.json) captured from
 * the Python implementation: such an algorithm cannot be verified by reasoning.
 *
 * Declarations use std types only, bodies live in Crypto.cpp, so that OpenSSL
 * headers do not leak into every translation unit (same reason as
 * utils/Crypto.hpp).
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The functions below throw Xiaomi::MiFitnessAuthError, so the type must be
// visible together with their declarations: otherwise EXPECT_THROW at the
// consumer fails to compile, and the compiler message points the wrong way.
#include "fitness/xiaomi/Errors.hpp"

namespace Xiaomi::Crypto {

/// Standard base64 with padding. Utils::Base64 does not fit here: it is
/// base64url without padding, while the Xiaomi protocol needs the plain alphabet.
std::string b64_encode(std::string_view raw);

/// Throws MiFitnessAuthError on any input that is not valid standard base64:
/// silently returning garbage is more dangerous here than failing.
std::string b64_decode(std::string_view encoded);

/**
 * @brief RC4 in the Xiaomi variant: 1024 bytes of the stream are discarded
 *        after key setup.
 *
 * Encryption and decryption are the same operation. An empty key is rejected:
 * in Python it raised on modulo by zero, in C++ it would be undefined
 * behavior.
 */
std::string rc4(std::string_view key, std::string_view payload);

/// SHA256(ssecurity || nonce), where ssecurity arrives in base64 and nonce as
/// raw bytes. Returns 32 raw bytes.
std::string signed_nonce(std::string_view ssecurity_b64, std::string_view nonce_raw);

/// 8 random bytes plus 4 big-endian bytes with the minute number since epoch.
/// Exactly 12 bytes: the random part's length is checked so that we never build
/// a short nonce the cloud would silently reject.
std::string make_nonce(std::int64_t minutes_since_epoch, std::string_view random8);

/// 8 random bytes from OpenSSL to pass into make_nonce.
std::string random_bytes(std::size_t count);

/// base64(SHA1(method & path & data=<data> [& rc4_hash__=<hash>] & base64(signed_nonce))).
/// The field order is mandatory: the signature is computed twice, before and
/// after encryption, both times by this same scheme.
std::string signature(std::string_view method,
                      std::string_view path,
                      std::string_view data,
                      std::optional<std::string_view> rc4_hash,
                      std::string_view signed_nonce_raw);

}  // namespace Xiaomi::Crypto
