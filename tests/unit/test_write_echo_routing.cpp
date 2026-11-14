/**
 * @file test_write_echo_routing.cpp
 * @brief The reads behind the answer to a write go to the primary: the steps
 *        goal after PUT /fitness/goals and the day's plan after PUT or DELETE
 *        /food/plan. A replica may not have the write yet, and the answer
 *        then said the old value. A plain GET still reads a replica.
 *
 *        Runs against the Database DI seam (same fake shape as
 *        test_crud_owned.cpp), no Postgres: the fake counts which pool each
 *        read asked for and returns empty results.
 */

#include <map>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "database/Database.hpp"
#include "repositories/fitness/GoalsRepository.hpp"
#include "repositories/food/PlanRepository.hpp"

namespace {

class EmptyTxn : public Database::detail::ErasedTxn {
public:
    pqxx::result exec_params_erased(const std::string& /*query*/, const pqxx::params& /*params*/) override {
        return pqxx::result{};
    }
    pqxx::result exec_erased(const std::string& /*query*/) override { return pqxx::result{}; }
};

class CountingDatabase : public Database::DatabaseManager {
public:
    EmptyTxn txn;
    std::map<std::string, int> entered;  // "<op>@<pool>" -> count

    bool is_initialized() const override { return true; }
    bool health_check() override { return true; }

protected:
    Database::detail::ErasedTxn* test_transaction_(const char* op, const char* pool) override {
        ++entered[std::string(op) + "@" + pool];
        return &txn;
    }
};

class WriteEchoRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto fake = std::make_unique<CountingDatabase>();
        fake_ = fake.get();
        Database::install_for_testing(std::move(fake));
    }
    void TearDown() override { Database::reset_for_testing(); }

    CountingDatabase* fake_ = nullptr;
};

TEST_F(WriteEchoRoutingTest, StepsGoalAfterAWriteReadsThePrimary) {
    Repositories::Fitness::GoalsRepository repo;
    const auto plain = repo.get("user-1");
    EXPECT_EQ(plain["steps"], Repositories::Fitness::GoalsRepository::kDefaultSteps);
    EXPECT_EQ(fake_->entered["db.read@replica"], 1);
    EXPECT_EQ(fake_->entered["db.read@primary"], 0);

    repo.get("user-1", /*from_primary=*/true);
    EXPECT_EQ(fake_->entered["db.read@replica"], 1);
    EXPECT_EQ(fake_->entered["db.read@primary"], 1);
}

TEST_F(WriteEchoRoutingTest, PlanDayAfterAWriteReadsThePrimary) {
    Repositories::PlanRepository repo;
    const auto plain = repo.day("user-1", "2026-11-14");
    EXPECT_EQ(plain["source"], "none");
    EXPECT_EQ(fake_->entered["db.read@replica"], 1);
    EXPECT_EQ(fake_->entered["db.read@primary"], 0);

    repo.day("user-1", "2026-11-14", /*from_primary=*/true);
    EXPECT_EQ(fake_->entered["db.read@replica"], 1);
    EXPECT_EQ(fake_->entered["db.read@primary"], 1);
}

}  // namespace
