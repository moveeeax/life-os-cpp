/**
 * @file Seal.hpp
 * @brief A secret kept in the database, sealed with libsodium secretbox under
 *        a server key (32 bytes, base64url without padding, from config). The
 *        calendar's refresh token is kept this way; the Mi account has its own
 *        copy of the scheme (fitness/xiaomi/Credentials.hpp) with its own error.
 */

#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace Utils::Seal {

struct SealError : std::runtime_error {
    explicit SealError(const std::string& what) : std::runtime_error(what) {}
};

struct Sealed {
    /// base64url of the ciphertext and of the nonce, ready for text columns.
    std::string ciphertext;
    std::string nonce;
};

/// Seals @p plain with a fresh nonce. @throws SealError for a key that is not 32 bytes.
Sealed seal(std::string_view plain, std::string_view key_b64url);

/// Opens what seal() made. @throws SealError for a wrong key or any tampering (no details).
std::string unseal(const Sealed& sealed, std::string_view key_b64url);

/// A new random key in the format seal() takes.
std::string new_key();

}  // namespace Utils::Seal
