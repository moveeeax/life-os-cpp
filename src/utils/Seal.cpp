/**
 * @file Seal.cpp
 * @brief Body for src/utils/Seal.hpp.
 */

#include "utils/Seal.hpp"

#include <sodium.h>

#include "utils/Base64.hpp"

namespace Utils::Seal {

namespace {

void ready() {
    static const int rc = ::sodium_init();
    if (rc < 0) {
        throw SealError("libsodium failed to initialize");
    }
}

std::string key_of(std::string_view key_b64url) {
    std::string key;
    try {
        key = Utils::Base64::url_decode(key_b64url);
    } catch (const std::exception&) {
        throw SealError("the seal key is not base64url");
    }
    if (key.size() != crypto_secretbox_KEYBYTES) {
        throw SealError("the seal key must be 32 bytes");
    }
    return key;
}

const unsigned char* bytes(const std::string& s) {
    return reinterpret_cast<const unsigned char*>(s.data());
}

}  // namespace

Sealed seal(std::string_view plain, std::string_view key_b64url) {
    ready();
    const std::string key = key_of(key_b64url);
    std::string nonce(crypto_secretbox_NONCEBYTES, '\0');
    ::randombytes_buf(nonce.data(), nonce.size());
    std::string out(plain.size() + crypto_secretbox_MACBYTES, '\0');
    if (::crypto_secretbox_easy(reinterpret_cast<unsigned char*>(out.data()),
                                reinterpret_cast<const unsigned char*>(plain.data()),
                                static_cast<unsigned long long>(plain.size()),
                                bytes(nonce),
                                bytes(key)) != 0) {
        throw SealError("sealing failed");
    }
    return Sealed{Utils::Base64::url_encode(out), Utils::Base64::url_encode(nonce)};
}

std::string unseal(const Sealed& sealed, std::string_view key_b64url) {
    ready();
    const std::string key = key_of(key_b64url);
    std::string box;
    std::string nonce;
    try {
        box = Utils::Base64::url_decode(sealed.ciphertext);
        nonce = Utils::Base64::url_decode(sealed.nonce);
    } catch (const std::exception&) {
        throw SealError("the sealed value is damaged");
    }
    if (nonce.size() != crypto_secretbox_NONCEBYTES || box.size() < crypto_secretbox_MACBYTES) {
        throw SealError("the sealed value is damaged");
    }
    std::string plain(box.size() - crypto_secretbox_MACBYTES, '\0');
    if (::crypto_secretbox_open_easy(reinterpret_cast<unsigned char*>(plain.data()),
                                     bytes(box),
                                     static_cast<unsigned long long>(box.size()),
                                     bytes(nonce),
                                     bytes(key)) != 0) {
        throw SealError("the sealed value does not open with this key");
    }
    return plain;
}

std::string new_key() {
    ready();
    std::string key(crypto_secretbox_KEYBYTES, '\0');
    ::randombytes_buf(key.data(), key.size());
    return Utils::Base64::url_encode(key);
}

}  // namespace Utils::Seal
