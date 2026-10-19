/**
 * @file test_ratelimit_redis.cpp
 * @brief Integration test for the RateLimit Redis enforcement path (the
 *        sliding-window EVAL). The unit tests cover disabled/whitelist/
 *        fail-open; this exercises the actual counter against live Redis.
 *
 * Suite name RateLimitRedisTest → integration bucket (needs Redis).
 */

#include <iterator>

#include <gtest/gtest.h>

#include "cache/Cache.hpp"
#include "security/RateLimit.hpp"
#include "test_helpers.hpp"

namespace RL = Security::RateLimit;

namespace {

class RateLimitRedisTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!TestHelpers::is_redis_available())
            return TestHelpers::skip_or_fail_without_infra("Redis");
        TestHelpers::reset_all_globals();
        Cache::initialize(TestHelpers::redis_url(), 2);
        // Clear any leftover window keys from a previous run.
        try {
            auto& r = Cache::get().get_client();
            std::vector<std::string> keys;
            r.keys("rl:sw:*", std::back_inserter(keys));
            r.keys("rl:auth:*", std::back_inserter(keys));
            r.keys("rl:authfail:*", std::back_inserter(keys));
            r.keys("rl:login:*", std::back_inserter(keys));
            for (const auto& k : keys)
                r.del(k);
        } catch (...) {}
    }
    void TearDown() override { TestHelpers::reset_all_globals(); }

    static RL::Config cfg(int requests) {
        RL::Config c;
        c.enabled = true;
        c.requests = requests;
        c.window_sec = 60;
        c.fail_open = true;
        return c;
    }
};

TEST_F(RateLimitRedisTest, AllowsUpToLimitThenDenies) {
    RL::Limiter lim(cfg(/*requests=*/3));
    const std::string id = "ip:10.1.2.3";

    for (int i = 0; i < 3; ++i) {
        auto d = lim.check(id);
        EXPECT_TRUE(d.allowed) << "request " << i << " should be allowed";
    }
    auto over = lim.check(id);
    EXPECT_FALSE(over.allowed) << "4th request in the window must be denied";
    EXPECT_GT(over.retry_after_sec, 0);
}

TEST_F(RateLimitRedisTest, SeparateIdentitiesHaveSeparateWindows) {
    RL::Limiter lim(cfg(/*requests=*/1));
    EXPECT_TRUE(lim.check("ip:1.1.1.1").allowed);
    EXPECT_FALSE(lim.check("ip:1.1.1.1").allowed);
    // A different identity is unaffected.
    EXPECT_TRUE(lim.check("ip:2.2.2.2").allowed);
}

TEST_F(RateLimitRedisTest, ProtectedTierIsSeparateAndStricter) {
    RL::Config c = cfg(/*requests=*/5);
    c.protected_requests = 1;
    c.protected_window_sec = 60;
    RL::Limiter lim(std::move(c));
    const std::string id = "ip:9.9.9.9";

    // Protected tier: 1 allowed, then denied (the strict auth cap).
    EXPECT_TRUE(lim.check_protected(id).allowed);
    EXPECT_FALSE(lim.check_protected(id).allowed) << "2nd hit on the auth tier must be denied";

    // The general tier on the SAME identity is untouched — separate key
    // namespace ("rl:sw:" vs "rl:auth:"), so a throttled login doesn't also
    // lock the caller out of the normal API.
    EXPECT_TRUE(lim.check(id).allowed) << "general tier must not share the auth-tier counter";
}

TEST_F(RateLimitRedisTest, FailureTierCountsOnlyRecordedFailures) {
    RL::Config c = cfg(/*requests=*/100);
    c.auth_fail_requests = 2;
    c.auth_fail_window_sec = 60;
    RL::Limiter lim(std::move(c));
    const std::string id = "ip:10.9.8.7";
    const auto tier = RL::FailureTier::AuthFailure;

    // Checks alone never spend the budget: a client that keeps presenting
    // good credentials is never throttled by this tier.
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(lim.check_failures(tier, id).allowed) << "check " << i;

    lim.record_failure(tier, id);
    EXPECT_TRUE(lim.check_failures(tier, id).allowed) << "one failure of two";
    lim.record_failure(tier, id);
    const auto refused = lim.check_failures(tier, id);
    EXPECT_FALSE(refused.allowed) << "two failures exhaust a budget of two";
    EXPECT_GT(refused.retry_after_sec, 0);
    EXPECT_LE(refused.retry_after_sec, 60);

    // Another IP, the general tier and the login tier keep their own counts.
    EXPECT_TRUE(lim.check_failures(tier, "ip:10.9.8.6").allowed);
    EXPECT_TRUE(lim.check(id).allowed);
    EXPECT_TRUE(lim.check_failures(RL::FailureTier::LoginEmail, id).allowed);
}

TEST_F(RateLimitRedisTest, FailureTierIsTrimmedToItsBudget) {
    RL::Config c = cfg(/*requests=*/100);
    c.login_email_requests = 3;
    c.login_email_window_sec = 60;
    RL::Limiter lim(std::move(c));
    const std::string id = RL::email_identity("trim@example.com");
    for (int i = 0; i < 50; ++i)
        lim.record_failure(RL::FailureTier::LoginEmail, id);
    EXPECT_FALSE(lim.check_failures(RL::FailureTier::LoginEmail, id).allowed);
    // A flood of failures keeps at most the budget's worth of entries.
    EXPECT_EQ(Cache::get().get_client().zcard("rl:login:" + id), 3);
}

}  // namespace
