#include <gtest/gtest.h>

#include "cache/Cache.hpp"
#include "security/RateLimit.hpp"

namespace RL = Security::RateLimit;

TEST(RateLimitTest, DisabledAlwaysAllows) {
    RL::Config cfg;
    cfg.enabled = false;
    cfg.requests = 1;
    RL::Limiter lim(std::move(cfg));
    for (int i = 0; i < 100; ++i) {
        auto d = lim.check("anyone");
        EXPECT_TRUE(d.allowed);
    }
}

TEST(RateLimitTest, WhitelistBypass) {
    RL::Config cfg;
    cfg.enabled = true;
    cfg.requests = 1;
    cfg.whitelist.insert("ip:127.0.0.1");
    RL::Limiter lim(std::move(cfg));
    for (int i = 0; i < 10; ++i) {
        auto d = lim.check("ip:127.0.0.1");
        EXPECT_TRUE(d.allowed);
    }
}

TEST(RateLimitTest, FailOpenWhenCacheUnavailable) {
    // Explicit precondition: a prior suite leaking an initialized Cache
    // would silently flip this into testing the Redis path instead.
    ASSERT_FALSE(Cache::is_initialized()) << "leaked Cache from a previous suite";
    RL::Config cfg;
    cfg.enabled = true;
    cfg.requests = 1;
    cfg.fail_open = true;
    RL::Limiter lim(std::move(cfg));
    auto d = lim.check("ip:1.2.3.4");
    EXPECT_TRUE(d.allowed);
}

TEST(RateLimitTest, FailClosedWhenCacheUnavailable) {
    ASSERT_FALSE(Cache::is_initialized()) << "leaked Cache from a previous suite";
    RL::Config cfg;
    cfg.enabled = true;
    cfg.requests = 1;
    cfg.fail_open = false;
    RL::Limiter lim(std::move(cfg));
    auto d = lim.check("ip:1.2.3.4");
    EXPECT_FALSE(d.allowed);
    EXPECT_GE(d.retry_after_sec, 1);
}

TEST(RateLimitTest, ProtectedTierDisabledAllows) {
    RL::Config cfg;
    cfg.enabled = false;
    cfg.protected_requests = 1;
    RL::Limiter lim(std::move(cfg));
    for (int i = 0; i < 50; ++i) {
        auto d = lim.check_protected("ip:1.2.3.4");
        EXPECT_TRUE(d.allowed);
    }
}

TEST(RateLimitTest, ProtectedTierFailOpenWhenCacheUnavailable) {
    ASSERT_FALSE(Cache::is_initialized()) << "leaked Cache from a previous suite";
    RL::Config cfg;
    cfg.enabled = true;
    cfg.protected_requests = 1;
    cfg.fail_open = true;
    RL::Limiter lim(std::move(cfg));
    auto d = lim.check_protected("ip:1.2.3.4");
    EXPECT_TRUE(d.allowed);
}

TEST(RateLimitTest, ProtectedTierWhitelistBypass) {
    RL::Config cfg;
    cfg.enabled = true;
    cfg.protected_requests = 1;
    cfg.whitelist.insert("ip:127.0.0.1");
    RL::Limiter lim(std::move(cfg));
    for (int i = 0; i < 10; ++i) {
        auto d = lim.check_protected("ip:127.0.0.1");
        EXPECT_TRUE(d.allowed);
    }
}

TEST(RateLimitTest, ParseScopeAllVariants) {
    EXPECT_EQ(RL::parse_scope("ip"), RL::Scope::Ip);
    EXPECT_EQ(RL::parse_scope("ip_or_user"), RL::Scope::IpOrUser);
    // "user" and any unknown string fall back to IpOrUser for safety —
    // we never want to lump every anonymous caller into one shared bucket.
    EXPECT_EQ(RL::parse_scope("user"), RL::Scope::IpOrUser);
    EXPECT_EQ(RL::parse_scope("anything-else"), RL::Scope::IpOrUser);
}

TEST(RateLimitTest, FailureTiersDisabledOrWhitelistedAllow) {
    RL::Config cfg;
    cfg.enabled = false;
    cfg.auth_fail_requests = 1;
    RL::Limiter off(cfg);
    off.record_failure(RL::FailureTier::AuthFailure, "ip:1.2.3.4");  // no cache, no throw
    EXPECT_TRUE(off.check_failures(RL::FailureTier::AuthFailure, "ip:1.2.3.4").allowed);

    cfg.enabled = true;
    cfg.whitelist.insert("10.0.0.1");
    RL::Limiter on(std::move(cfg));
    EXPECT_TRUE(on.check_failures(RL::FailureTier::AuthFailure, "ip:10.0.0.1").allowed);
}

TEST(RateLimitTest, FailureTierFollowsFailOpenWithoutCache) {
    ASSERT_FALSE(Cache::is_initialized()) << "leaked Cache from a previous suite";
    RL::Config cfg;
    cfg.enabled = true;
    cfg.fail_open = false;
    RL::Limiter closed(cfg);
    EXPECT_FALSE(closed.check_failures(RL::FailureTier::LoginEmail, "email:x").allowed);
    cfg.fail_open = true;
    RL::Limiter open(std::move(cfg));
    EXPECT_TRUE(open.check_failures(RL::FailureTier::LoginEmail, "email:x").allowed);
}

TEST(RateLimitTest, EmailIdentityIgnoresCaseAndSpacesAndHidesTheAddress) {
    const std::string id = RL::email_identity("Anna@Example.com");
    EXPECT_EQ(id, RL::email_identity("  anna@example.com\t"));
    EXPECT_NE(id, RL::email_identity("boris@example.com"));
    EXPECT_EQ(id.rfind("email:", 0), 0u);
    EXPECT_EQ(id.find("anna"), std::string::npos) << "the key must not carry the address";
}
