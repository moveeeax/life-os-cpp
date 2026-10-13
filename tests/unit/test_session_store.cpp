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
