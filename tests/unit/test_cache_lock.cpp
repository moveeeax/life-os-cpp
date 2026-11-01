/**
 * @file test_cache_lock.cpp
 * @brief Cache::Lock: one holder at a time, and a holder that outlived its
 *        TTL never deletes the lock the next holder took (InMemoryCache, no
 *        Redis).
 */

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "InMemoryCache.hpp"
#include "cache/Cache.hpp"
#include "cache/Lock.hpp"

namespace {

struct CacheLockTest : ::testing::Test {
    void SetUp() override { Cache::install_for_testing(std::make_unique<TestSupport::InMemoryCache>()); }
    void TearDown() override { Cache::reset_for_testing(); }
};

TEST_F(CacheLockTest, OneHolderAtATime) {
    std::optional<Cache::Lock> first;
    first.emplace("job:lock", std::chrono::seconds(60));
    EXPECT_TRUE(first->held());
    {
        Cache::Lock second("job:lock", std::chrono::seconds(60));
        EXPECT_FALSE(second.held());
    }
    EXPECT_TRUE(Cache::get().exists("job:lock")) << "a lock not taken releases nothing";
    first.reset();
    EXPECT_FALSE(Cache::get().exists("job:lock"));
    Cache::Lock third("job:lock", std::chrono::seconds(60));
    EXPECT_TRUE(third.held());
}

TEST_F(CacheLockTest, AnExpiredHolderLeavesTheNextOnesLock) {
    std::optional<Cache::Lock> slow;
    slow.emplace("job:lock", std::chrono::seconds(60));
    ASSERT_TRUE(slow->held());
    // Its TTL ran out (the fake does not expire keys) and another holder took the key.
    Cache::get().del("job:lock");
    Cache::Lock next("job:lock", std::chrono::seconds(60));
    ASSERT_TRUE(next.held());
    slow.reset();
    EXPECT_TRUE(Cache::get().exists("job:lock")) << "the slow holder's release is compare-and-delete";
}

TEST_F(CacheLockTest, DeleteIfEqualsComparesTheValue) {
    Cache::get().set("k", "mine");
    EXPECT_FALSE(Cache::get().del_if_equals("k", "theirs"));
    EXPECT_TRUE(Cache::get().exists("k"));
    EXPECT_TRUE(Cache::get().del_if_equals("k", "mine"));
    EXPECT_FALSE(Cache::get().exists("k"));
    EXPECT_FALSE(Cache::get().del_if_equals("k", "mine")) << "gone already";
}

TEST(CacheLockWithoutCache, CountsAsHeldOnlyWhenAsked) {
    Cache::reset_for_testing();
    EXPECT_TRUE(Cache::Lock("k", std::chrono::seconds(1)).held());
    EXPECT_FALSE(Cache::Lock("k", std::chrono::seconds(1), /*held_without_cache=*/false).held());
}

}  // namespace
