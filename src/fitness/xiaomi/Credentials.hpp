/**
 * @file Credentials.hpp
 * @brief Xiaomi account credentials: token validation, masking, sealing.
 *
 * Xiaomi rotates the passToken on every login, so the cluster Secret holds
 * only the initial value, while the live one sits encrypted in Postgres. The
 * encryption key comes from the environment and never reaches the database.
 *
 * Declarations use std types only, bodies live in Credentials.cpp: libsodium
 * headers must not leak out (same reason as utils/Crypto.hpp).
 */

#pragma once

#include <string>
#include <string_view>

#include "fitness/xiaomi/Errors.hpp"

namespace Xiaomi {

/// Credentials of one account. The region belongs here too: it determines both
/// the cloud host and the time zone in which day boundaries are computed.
struct Credentials {
    std::string user_id;
    std::string pass_token;
    std::string region;
};

/// Sealing result: raw bytes, no encoding. The repository base64-encodes them
/// when storing into TEXT columns.
struct Sealed {
    std::string ciphertext;
    std::string nonce;
};

/**
 * @brief Rejects a token that cannot be put into a Cookie header.
 *
 * The header must be ASCII without whitespace and without a semicolon. A real
 * case: a value copied from the DevTools cookie table arrives with U+2026 in
 * the middle, and without this check the failure happens only at the network
 * request with an obscure encoding error. The message names the position of
 * the bad character and never the value itself: the error text ends up in logs.
 */
void validate_pass_token(std::string_view token);

/// Six stars and the last two characters. The original length cannot be
/// recovered from the mask. Empty input gives an empty string.
std::string mask_account_id(std::string_view value);

/// crypto_secretbox_easy with a fresh nonce. The key arrives in base64 and must
/// be crypto_secretbox_KEYBYTES long, otherwise MiFitnessAuthError.
Sealed seal(std::string_view plain, std::string_view key_b64);

/// The inverse operation. A wrong key and any ciphertext tampering give
/// MiFitnessAuthError without details: details help nothing here and would
/// end up in the log.
std::string unseal(const Sealed& sealed, std::string_view key_b64);

}  // namespace Xiaomi
