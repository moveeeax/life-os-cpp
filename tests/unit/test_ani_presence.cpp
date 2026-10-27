/**
 * @file test_ani_presence.cpp
 * @brief Agent::Presence: the open Now page asks for Ani's inbox every few
 *        seconds; the time of the last ask says whether the page is open.
 *        Runs on the in-memory cache.
 */

#include <chrono>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "InMemoryCache.hpp"
#include "agent/Presence.hpp"
#include "cache/Cache.hpp"

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaac9";

class AniPresence : public ::testing::Test {
protected:
    void SetUp() override { Cache::install_for_testing(std::make_unique<TestSupport::InMemoryCache>()); }
    void TearDown() override { Cache::reset_for_testing(); }
};

TEST_F(AniPresence, PresenceFromPolls) {
    EXPECT_FALSE(Agent::Presence::seen_ago(kAnna).has_value()) << "never seen";
    Agent::Presence::mark(kAnna);
    const auto now_ago = Agent::Presence::seen_ago(kAnna);
    ASSERT_TRUE(now_ago.has_value());
    EXPECT_GE(*now_ago, 0);
    EXPECT_LE(*now_ago, 2);

    const long long then =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() -
        30;
    Cache::get().set(std::string("ani:seen:") + kAnna, std::to_string(then), 3600);
    const auto later = Agent::Presence::seen_ago(kAnna);
    ASSERT_TRUE(later.has_value());
    EXPECT_NEAR(*later, 30, 2);
    EXPECT_GT(*later, Agent::Presence::kOpenSeconds) << "so the page reads as away";
}

TEST_F(AniPresence, NoCacheMeansUnknown) {
    Cache::reset_for_testing();
    Agent::Presence::mark(kAnna);
    EXPECT_FALSE(Agent::Presence::seen_ago(kAnna).has_value());
}

}  // namespace
