/**
 * @file test_xiaomi_credentials.cpp
 * @brief Token validation, identifier masking, secretbox. No database.
 */

#include <string>

#include <gtest/gtest.h>

#include "fitness/xiaomi/Credentials.hpp"

namespace {
// 32 zero bytes in base64: the encryption key for tests.
constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
constexpr const char* kOtherKeyB64 = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=";
}  // namespace

// Exactly the case that once cost an hour of debugging: DevTools shows a long
// cookie with an ellipsis in the middle, and the copied value arrives with
// U+2026 inside. The Cookie header must be ASCII, so this used to fail only at
// the network request with an obscure encoding error.
TEST(XiaomiCredentials, RejectsNonAsciiToken) {
    const std::string token = std::string("AXSU") + "\xe2\x80\xa6" + "CqGAl";
    try {
        Xiaomi::validate_pass_token(token);
        FAIL() << "expected rejection on a non-ASCII character";
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        // The position lets the owner see where the value got cut.
        EXPECT_NE(std::string(e.what()).find("position 4"), std::string::npos) << "message: " << e.what();
        // The value itself must not appear in the error text: it ends up in logs.
        EXPECT_EQ(std::string(e.what()).find("AXSU"), std::string::npos);
    }
}

TEST(XiaomiCredentials, RejectsSemicolonWhitespaceAndEmpty) {
    EXPECT_THROW(Xiaomi::validate_pass_token("abc;def"), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(Xiaomi::validate_pass_token("abc def"), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(Xiaomi::validate_pass_token("abc\tdef"), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(Xiaomi::validate_pass_token("abc\ndef"), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(Xiaomi::validate_pass_token(""), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiCredentials, AcceptsRealisticToken) {
    // A real token is 347 printable ASCII characters.
    EXPECT_NO_THROW(Xiaomi::validate_pass_token(std::string(347, 'A')));
}

TEST(XiaomiCredentials, MaskKeepsOnlyLastTwoCharacters) {
    EXPECT_EQ(Xiaomi::mask_account_id("1234567890"), "******90");
    EXPECT_EQ(Xiaomi::mask_account_id("42"), "******");
    EXPECT_EQ(Xiaomi::mask_account_id("4"), "******");
    EXPECT_EQ(Xiaomi::mask_account_id(""), "");
    // The original length cannot be recovered from the mask: always six stars.
    EXPECT_EQ(Xiaomi::mask_account_id("123456789012345").size(), 8u);
}

TEST(XiaomiCredentials, SealRoundTrips) {
    const std::string plain(347, 'T');
    const Xiaomi::Sealed sealed = Xiaomi::seal(plain, kTestKeyB64);
    EXPECT_NE(sealed.ciphertext, plain);
    EXPECT_EQ(sealed.nonce.size(), 24u);
    // The ciphertext is longer than the plaintext by the auth tag.
    EXPECT_EQ(sealed.ciphertext.size(), plain.size() + 16u);
    EXPECT_EQ(Xiaomi::unseal(sealed, kTestKeyB64), plain);
}

TEST(XiaomiCredentials, SealUsesAFreshNonceEveryTime) {
    const Xiaomi::Sealed first = Xiaomi::seal("token", kTestKeyB64);
    const Xiaomi::Sealed second = Xiaomi::seal("token", kTestKeyB64);
    EXPECT_NE(first.nonce, second.nonce);
    EXPECT_NE(first.ciphertext, second.ciphertext);
}

TEST(XiaomiCredentials, UnsealWithWrongKeyFails) {
    const Xiaomi::Sealed sealed = Xiaomi::seal("secret", kTestKeyB64);
    EXPECT_THROW(Xiaomi::unseal(sealed, kOtherKeyB64), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiCredentials, UnsealDetectsTamperedCiphertext) {
    Xiaomi::Sealed sealed = Xiaomi::seal("secret", kTestKeyB64);
    sealed.ciphertext[0] = static_cast<char>(sealed.ciphertext[0] ^ 0x01);
    EXPECT_THROW(Xiaomi::unseal(sealed, kTestKeyB64), Xiaomi::MiFitnessAuthError);
}

TEST(XiaomiCredentials, RejectsKeyOfWrongLength) {
    EXPECT_THROW(Xiaomi::seal("secret", "QUJD"), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(Xiaomi::seal("secret", ""), Xiaomi::MiFitnessAuthError);
}
