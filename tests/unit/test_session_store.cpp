/**
 * @file test_session_store.cpp
 * @brief Refresh-token revocation store on the in-memory cache fake: a
 *        refresh JTI is consumed by exactly one caller, so two refreshes
 *        racing with the same token cannot both mint a session.
 */

#include <memory>

#include <gtest/gtest.h>

#include "InMemoryCache.hpp"
#include "cache/Cache.hpp"
#include "security/SessionStore.hpp"
#include "utils/Time.hpp"

class SessionStoreTest : public ::testing::Test {
protected:
    Security::Auth::CookieConfig cookies;

    void SetUp() override { Cache::install_for_testing(std::make_unique<TestSupport::InMemoryCache>()); }
    void TearDown() override { Cache::reset_for_testing(); }
};

TEST_F(SessionStoreTest, ARefreshJtiIsConsumedOnce) {
    ASSERT_TRUE(Security::Sessions::record(cookies, "user-1", "jti-1", 60));
    EXPECT_TRUE(Security::Sessions::consume(cookies, "jti-1")) << "the first refresh takes it";
    EXPECT_FALSE(Security::Sessions::consume(cookies, "jti-1")) << "a replay or a racing refresh finds it gone";
}

TEST_F(SessionStoreTest, AnUnknownJtiIsNotConsumed) {
    EXPECT_FALSE(Security::Sessions::consume(cookies, "never-issued"));
}

TEST(SessionStoreNoCache, ConsumeFailsClosedWithoutACache) {
    Cache::reset_for_testing();
    Security::Auth::CookieConfig cookies;
    EXPECT_FALSE(Security::Sessions::consume(cookies, "jti-1"));
}

// -- Access tokens die at revoke_all (the per-user valid-since marker) --

TEST_F(SessionStoreTest, AccessTokensIssuedBeforeRevokeAllAreRevoked) {
    const auto now_ms = Utils::Time::now_epoch_millis();
    const Security::Auth::json before = {{"sub", "user-1"}, {"iat", now_ms / 1000 - 5}, {"iat_ms", now_ms - 5000}};
    EXPECT_FALSE(Security::Sessions::access_revoked(cookies, "user-1", before)) << "no marker yet";

    Security::Sessions::revoke_all(cookies, "user-1");
    EXPECT_TRUE(Security::Sessions::access_revoked(cookies, "user-1", before));

    const auto later_ms = Utils::Time::now_epoch_millis() + 1;
    const Security::Auth::json after = {{"sub", "user-1"}, {"iat", later_ms / 1000}, {"iat_ms", later_ms}};
    EXPECT_FALSE(Security::Sessions::access_revoked(cookies, "user-1", after)) << "a session minted after it lives";

    // Another user is untouched.
    EXPECT_FALSE(Security::Sessions::access_revoked(cookies, "user-2", before));
}

TEST_F(SessionStoreTest, ATokenWithoutIatMsFallsBackToIatAndOneWithoutAnyIatIsRevoked) {
    Security::Sessions::mark_access_revoked(cookies, "user-1");
    const auto now_s = Utils::Time::now_epoch_seconds();
    EXPECT_TRUE(Security::Sessions::access_revoked(cookies, "user-1", {{"iat", now_s - 60}}));
    EXPECT_FALSE(Security::Sessions::access_revoked(cookies, "user-1", {{"iat", now_s + 60}}));
    EXPECT_TRUE(Security::Sessions::access_revoked(cookies, "user-1", Security::Auth::json::object()));
}

TEST(SessionStoreNoCache, AccessRevokedFailsOpenWithoutACache) {
    Cache::reset_for_testing();
    Security::Auth::CookieConfig cookies;
    EXPECT_FALSE(Security::Sessions::access_revoked(cookies, "user-1", {{"iat", 0}}));
}
