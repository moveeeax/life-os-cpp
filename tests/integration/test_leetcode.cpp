/**
 * @file test_leetcode.cpp
 * @brief LeetCode in Life OS against a real database and a scripted HTTP
 *        transport: a sync stores today's solved counts, the series
 *        leetcode:solved is the day's growth over the previous snapshot, and
 *        a habit can take it as its source.
 */

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "habits/Habits.hpp"
#include "leetcode/Sync.hpp"
#include "metrics/Catalog.hpp"
#include "net/Http.hpp"
#include "repositories/leetcode/ProfileRepository.hpp"
#include "repositories/metrics/Series.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::LeetCode::ProfileRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaacce";

struct FakeLeetCode : Net::Http::Transport {
    int total = 807;
    int posts = 0;
    std::string last_body;
    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        return {404, ""};
    }
    Net::Http::Response post_json(const std::string& url,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        ++posts;
        last_body = body;
        if (url != "https://leetcode.com/graphql")
            return {404, ""};
        json counts = json::array();
        counts.push_back({{"difficulty", "All"}, {"count", total}});
        counts.push_back({{"difficulty", "Easy"}, {"count", 208}});
        counts.push_back({{"difficulty", "Medium"}, {"count", 451}});
        counts.push_back({{"difficulty", "Hard"}, {"count", total - 659}});
        json user = json::object();
        user["username"] = "moveeeax";
        user["submitStatsGlobal"]["acSubmissionNum"] = counts;
        json answer = json::object();
        answer["data"]["matchedUser"] = user;
        return {200, answer.dump()};
    }
};

/// LeetCode refusing (rate limit): no snapshot, the error kept.
struct RateLimited : FakeLeetCode {
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        return {429, "rate limited"};
    }
};

class LeetCodeSync : public TestHelpers::CoreBackedTest {
protected:
    FakeLeetCode leetcode;
    ProfileRepository profiles;

    std::string config_file_name() const override { return "leetcode_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM leetcode_snapshots");
            txn.exec("DELETE FROM leetcode_profiles");
            return true;
        });
        Net::Http::install_for_testing(&leetcode);
    }
    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }

    static void snapshot(const std::string& day, int total) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO leetcode_snapshots (owner_id, day, total, easy, medium, hard) VALUES ($1::uuid, "
                "$2::date, $3, 0, 0, 0) ON CONFLICT (owner_id, day) DO UPDATE SET total = EXCLUDED.total",
                std::string(kAnna),
                day,
                total);
            return true;
        });
    }
};

}  // namespace

TEST_F(LeetCodeSync, SyncStoresToday) {
    profiles.set(kAnna, "moveeeax", "Asia/Almaty");
    LeetCode::process_sync(json::object());
    EXPECT_EQ(leetcode.posts, 1);
    EXPECT_NE(leetcode.last_body.find("moveeeax"), std::string::npos);
    const auto p = profiles.get(kAnna);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ((*p)["total"], 807);
    EXPECT_TRUE((*p)["last_error"].is_null());
}

// A saved profile pulls only its owner's: saving used to queue a pull of every user's profile.
TEST_F(LeetCodeSync, ASavedProfilePullsOnlyItsOwner) {
    profiles.set(kAnna, "moveeeax", "UTC");
    profiles.set("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbcce", "someone_else", "UTC");
    const auto out = LeetCode::process_sync(json{{"owner", kAnna}});
    EXPECT_EQ(leetcode.posts, 1);
    EXPECT_EQ(out["ok"], 1);
    EXPECT_NE(leetcode.last_body.find("moveeeax"), std::string::npos);
    LeetCode::process_sync(json::object());
    EXPECT_EQ(leetcode.posts, 3) << "the hourly pull reads every profile";
}

TEST_F(LeetCodeSync, FailedPullKeepsTheDay) {
    profiles.set(kAnna, "moveeeax", "UTC");
    RateLimited down;
    Net::Http::install_for_testing(&down);
    EXPECT_NO_THROW(LeetCode::process_sync(json::object()));
    const auto p = profiles.get(kAnna);
    ASSERT_TRUE(p.has_value());
    EXPECT_FALSE((*p)["last_error"].is_null());
    EXPECT_TRUE((*p)["total"].is_null()) << "no snapshot from a failed pull";
}

TEST_F(LeetCodeSync, SolvedIsTheDayGrowth) {
    profiles.set(kAnna, "moveeeax", "UTC");
    snapshot("2026-10-25", 800);
    snapshot("2026-10-26", 803);
    snapshot("2026-10-28", 807);
    const auto ref = Metrics::parse("leetcode:solved");
    ASSERT_TRUE(ref.has_value());
    Repositories::Metrics::Access a;
    a.leetcode = true;
    const auto s =
        Repositories::Metrics::read(kAnna, *ref, Habits::day_of("2026-10-26"), Habits::day_of("2026-10-28"), "UTC", a);
    ASSERT_EQ(s.why, Repositories::Metrics::Why::Ok);
    EXPECT_EQ(s.values.at(Habits::day_of("2026-10-26")), 3.0);
    EXPECT_EQ(s.values.count(Habits::day_of("2026-10-27")), 0u) << "no snapshot, no value";
    EXPECT_EQ(s.values.at(Habits::day_of("2026-10-28")), 4.0) << "over the last snapshot before it";
}

TEST_F(LeetCodeSync, NotLinkedWithoutProfile) {
    const auto ref = Metrics::parse("leetcode:solved");
    ASSERT_TRUE(ref.has_value());
    Repositories::Metrics::Access a = Repositories::Metrics::access_of(kAnna, 0xFFFFFFFFu);
    EXPECT_FALSE(a.leetcode);
    const auto s =
        Repositories::Metrics::read(kAnna, *ref, Habits::day_of("2026-10-26"), Habits::day_of("2026-10-28"), "UTC", a);
    EXPECT_EQ(s.why, Repositories::Metrics::Why::NotLinked);
}
