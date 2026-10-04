/**
 * @file test_xiaomi_crypto.cpp
 * @brief The Xiaomi crypto port against golden vectors from the Python implementation.
 *
 * The vectors in tests/fixtures/xiaomi_crypto_vectors.json were captured from
 * upstream with tools/gen_crypto_vectors.py. They are the whole point: Xiaomi's
 * RC4 is non-standard (1024 bytes of keystream are dropped after key setup),
 * and the signature assembly order cannot be verified by reasoning. The vectors
 * catch a porting error without a network and without a live account.
 */

#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Crypto.hpp"

namespace {

nlohmann::json load_vectors() {
    // Path from the repository root: the e2e tests read the spec the same way.
    std::ifstream in("tests/fixtures/xiaomi_crypto_vectors.json");
    if (!in) {
        throw std::runtime_error("fixture not found; run tools/gen_crypto_vectors.py from the repo root");
    }
    nlohmann::json j;
    in >> j;
    return j;
}

}  // namespace

TEST(XiaomiCrypto, Rc4MatchesUpstreamVectors) {
    // The fixture is kept in a named variable rather than an expression like
    // load_vectors()["rc4"]: there operator[] would return a reference into a
    // temporary that dies at the end of the expression, and the loop would walk
    // freed memory. Only a sanitizer catches that.
    const nlohmann::json fixture = load_vectors();
    const auto& vectors = fixture["rc4"];
    ASSERT_EQ(vectors.size(), 12u) << "fixture does not match what the test expects";
    for (const auto& v : vectors) {
        const auto key = Xiaomi::Crypto::b64_decode(v["key_b64"].get<std::string>());
        const auto payload = Xiaomi::Crypto::b64_decode(v["payload_b64"].get<std::string>());
        EXPECT_EQ(Xiaomi::Crypto::b64_encode(Xiaomi::Crypto::rc4(key, payload)), v["cipher_b64"].get<std::string>());
    }
}

TEST(XiaomiCrypto, Rc4IsItsOwnInverse) {
    const std::string key = "secret-material!";
    const std::string clear = R"({"start_time":1,"end_time":2})";
    EXPECT_EQ(Xiaomi::Crypto::rc4(key, Xiaomi::Crypto::rc4(key, clear)), clear);
}

TEST(XiaomiCrypto, SignedNonceMatchesUpstreamVectors) {
    const nlohmann::json fixture = load_vectors();
    for (const auto& v : fixture["signed_nonce"]) {
        const auto nonce = Xiaomi::Crypto::b64_decode(v["nonce_b64"].get<std::string>());
        EXPECT_EQ(
            Xiaomi::Crypto::b64_encode(Xiaomi::Crypto::signed_nonce(v["ssecurity_b64"].get<std::string>(), nonce)),
            v["signed_nonce_b64"].get<std::string>());
    }
}

TEST(XiaomiCrypto, SignatureMatchesUpstreamVectors) {
    const nlohmann::json fixture = load_vectors();
    for (const auto& v : fixture["signature"]) {
        const auto sn = Xiaomi::Crypto::b64_decode(v["signed_nonce_b64"].get<std::string>());
        std::string hash_storage;
        std::optional<std::string_view> rc4_hash;
        if (!v["rc4_hash"].is_null()) {
            hash_storage = v["rc4_hash"].get<std::string>();
            rc4_hash = hash_storage;
        }
        EXPECT_EQ(Xiaomi::Crypto::signature(v["method"].get<std::string>(),
                                            v["path"].get<std::string>(),
                                            v["data"].get<std::string>(),
                                            rc4_hash,
                                            sn),
                  v["signature"].get<std::string>());
    }
}

// An empty key in Python raised an exception on modulo by zero.
// In C++ that is undefined behavior, so the rejection must be explicit.
TEST(XiaomiCrypto, EmptyKeyIsRejected) {
    EXPECT_THROW(Xiaomi::Crypto::rc4("", "payload"), Xiaomi::MiFitnessAuthError);
}

// Nonce: 8 random bytes plus 4 big-endian bytes with the minute number.
TEST(XiaomiCrypto, NonceLayoutIsTwelveBytesBigEndianMinutes) {
    const std::string nonce = Xiaomi::Crypto::make_nonce(0x01020304, std::string(8, '\x00'));
    ASSERT_EQ(nonce.size(), 12u);
    EXPECT_EQ(static_cast<unsigned char>(nonce[8]), 0x01);
    EXPECT_EQ(static_cast<unsigned char>(nonce[9]), 0x02);
    EXPECT_EQ(static_cast<unsigned char>(nonce[10]), 0x03);
    EXPECT_EQ(static_cast<unsigned char>(nonce[11]), 0x04);
}

TEST(XiaomiCrypto, NonceRejectsWrongRandomLength) {
    EXPECT_THROW(Xiaomi::Crypto::make_nonce(1, std::string(7, '\x00')), Xiaomi::MiFitnessAuthError);
}

// Standard base64 with padding, not the base64url from Utils::Base64.
TEST(XiaomiCrypto, Base64IsStandardWithPadding) {
    EXPECT_EQ(Xiaomi::Crypto::b64_encode(std::string("\xff\xfe", 2)), "//4=");
    EXPECT_EQ(Xiaomi::Crypto::b64_decode("//4="), std::string("\xff\xfe", 2));
    EXPECT_EQ(Xiaomi::Crypto::b64_encode(""), "");
    // Two padding characters: one output byte, the decoder's shortest path.
    EXPECT_EQ(Xiaomi::Crypto::b64_decode("QQ=="), "A");
    EXPECT_EQ(Xiaomi::Crypto::b64_encode("A"), "QQ==");
    // Padding in the middle is garbage, not a short group.
    EXPECT_THROW(Xiaomi::Crypto::b64_decode("QQ==QQ=="), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(Xiaomi::Crypto::b64_decode("not base64!"), Xiaomi::MiFitnessAuthError);
}
