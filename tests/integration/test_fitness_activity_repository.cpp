/**
 * @file test_activity_repository.cpp
 * @brief Idempotent upsert of daily activity: added, then updated.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "database/Database.hpp"
#include "repositories/fitness/ActivityRepository.hpp"
#include "test_helpers.hpp"

namespace {

class ActivityRepositoryTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "activity_repo_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE daily_activity");
            return true;
        });
    }

    static long count_rows() {
        return Database::get().execute_read([](auto& txn) {
            auto r = txn.exec("SELECT COUNT(*) FROM daily_activity");
            return r[0][0].template as<long>();
        });
    }

    static Domain::DailyActivity day(const std::string& date, long steps) {
        Domain::DailyActivity out;
        out.user_id = "1234567890";
        out.date = date;
        out.timezone = "Asia/Shanghai";
        out.collected_at = date + "T21:00:00+08:00";
        out.steps = steps;
        out.distance_m = steps * 0.6;
        out.active_kcal = steps * 0.04;
        return out;
    }
};

}  // namespace

TEST_F(ActivityRepositoryTest, FirstUpsertAddsSecondUpdates) {
    Repositories::ActivityRepository repo;

    const auto first = repo.upsert({day("2026-09-22", 9454), day("2026-09-23", 3628)});
    EXPECT_EQ(first.added, 2);
    EXPECT_EQ(first.updated, 0);

    // Re-syncing the same range: rows do not multiply (rule 7).
    const auto second = repo.upsert({day("2026-09-22", 9454), day("2026-09-23", 3628)});
    EXPECT_EQ(second.added, 0);
    EXPECT_EQ(second.updated, 2);
    EXPECT_EQ(count_rows(), 2);
}

TEST_F(ActivityRepositoryTest, LaterCorrectionOverwritesTheDay) {
    Repositories::ActivityRepository repo;
    repo.upsert({day("2026-09-28", 100)});
    // Xiaomi backfills history after the fact: 100 steps in the morning, 2303 in the evening.
    repo.upsert({day("2026-09-28", 2303)});

    const long steps = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec("SELECT steps FROM daily_activity WHERE date = '2026-09-28'");
        return r[0][0].template as<long>();
    });
    EXPECT_EQ(steps, 2303);
    EXPECT_EQ(count_rows(), 1);
}

// A zero from the server is NULL in the database, not zero (rule 2): a day
// without distance stores NULL, and the column average is not diluted by zeros.
TEST_F(ActivityRepositoryTest, MissingOptionalsAreStoredAsNull) {
    Repositories::ActivityRepository repo;
    Domain::DailyActivity sparse = day("2026-09-24", 500);
    sparse.distance_m.reset();
    sparse.active_kcal.reset();
    repo.upsert({sparse});

    const bool distance_null = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec("SELECT distance_m IS NULL FROM daily_activity WHERE date = '2026-09-24'");
        return r[0][0].template as<bool>();
    });
    EXPECT_TRUE(distance_null);
}
