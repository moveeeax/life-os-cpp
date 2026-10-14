/**
 * @file test_user_repository.cpp
 * @brief UserRepository::remove beyond the owner_id tables: the fitness rows
 *        of the linked Xiaomi account, and which foreign-key violations mean
 *        billing history.
 */

#include <pqxx/pqxx>
#include <string>

#include <gtest/gtest.h>

#include "database/Database.hpp"
#include "repositories/SqlErrors.hpp"
#include "repositories/UserRepository.hpp"
#include "test_helpers.hpp"

namespace {

constexpr const char* kLeaver = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaa41";
constexpr const char* kStayer = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbb41";

class UserRepositoryRemoveTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "user_repository_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        exec("DROP TABLE IF EXISTS user_remove_test_ref");
        exec("DELETE FROM wallet_balances WHERE user_id IN ('" + std::string(kLeaver) + "', '" + kStayer + "')");
        exec("DELETE FROM mi_accounts WHERE owner_id IN ('" + std::string(kLeaver) + "', '" + kStayer + "')");
        exec("DELETE FROM daily_activity WHERE user_id IN ('4100000001', '4100000002')");
        exec("DELETE FROM sync_runs WHERE xiaomi_user_id IN ('4100000001', '4100000002')");
        for (const char* id : {kLeaver, kStayer}) {
            exec(std::string("INSERT INTO users (id, email, confirmed, role_id) VALUES ('") + id + "', '" + id +
                 "@example.test', TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING");
        }
    }

    void TearDown() override {
        if (Database::is_initialized()) {
            try {
                exec("DROP TABLE IF EXISTS user_remove_test_ref");
                exec("DELETE FROM wallet_balances WHERE user_id IN ('" + std::string(kLeaver) + "', '" + kStayer +
                     "')");
                exec("DELETE FROM daily_activity WHERE user_id IN ('4100000001', '4100000002')");
                exec("DELETE FROM sync_runs WHERE xiaomi_user_id IN ('4100000001', '4100000002')");
                exec("DELETE FROM users WHERE id IN ('" + std::string(kLeaver) + "', '" + kStayer + "')");
            } catch (...) {}
        }
        TestHelpers::CoreBackedTest::TearDown();
    }

    static void exec(const std::string& sql) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec(sql);
            return true;
        });
    }

    static long count(const std::string& sql) {
        return Database::get().execute_read([&](auto& txn) { return txn.exec(sql)[0][0].template as<long>(); });
    }

    /// A link row as the repository would write it; the token is never read here.
    static void link(const char* owner, const char* xiaomi_id) {
        exec(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce) VALUES ('") +
             owner + "', '" + xiaomi_id + "', 'sealed', 'nonce')");
    }
};

}  // namespace

TEST_F(UserRepositoryRemoveTest, TheFitnessRowsOfTheLinkedAccountGoWithTheUser) {
    link(kLeaver, "4100000001");
    link(kStayer, "4100000002");
    exec(
        "INSERT INTO daily_activity (user_id, date, steps) VALUES "
        "('4100000001', '2026-10-01', 9000), ('4100000002', '2026-10-01', 7000)");
    exec(
        "INSERT INTO sync_runs (status, finished_at, xiaomi_user_id) VALUES "
        "('succeeded', now(), '4100000001'), ('succeeded', now(), '4100000002')");

    Repositories::UserRepository().remove(kLeaver);

    EXPECT_EQ(count("SELECT count(*) FROM daily_activity WHERE user_id = '4100000001'"), 0)
        << "the health rows are keyed by the Xiaomi id and must not outlive the user";
    EXPECT_EQ(count("SELECT count(*) FROM sync_runs WHERE xiaomi_user_id = '4100000001'"), 0);
    EXPECT_EQ(count(std::string("SELECT count(*) FROM mi_accounts WHERE owner_id = '") + kLeaver + "'"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM daily_activity WHERE user_id = '4100000002'"), 1) << "another account stays";
    EXPECT_EQ(count("SELECT count(*) FROM sync_runs WHERE xiaomi_user_id = '4100000002'"), 1);
}

TEST_F(UserRepositoryRemoveTest, AUserWithoutALinkIsDeletedAsBefore) {
    Repositories::UserRepository().remove(kLeaver);
    EXPECT_EQ(count(std::string("SELECT count(*) FROM users WHERE id = '") + kLeaver + "'"), 0);
    EXPECT_THROW(Repositories::UserRepository().remove(kLeaver), Repositories::UserNotFound);
}

TEST_F(UserRepositoryRemoveTest, WalletHistoryIsBillingHistory) {
    exec(std::string("INSERT INTO wallet_balances (user_id, credits) VALUES ('") + kLeaver + "', 0)");
    EXPECT_THROW(Repositories::UserRepository().remove(kLeaver), Repositories::UserHasBillingHistory);
    EXPECT_EQ(count(std::string("SELECT count(*) FROM users WHERE id = '") + kLeaver + "'"), 1)
        << "the transaction rolled back";
}

TEST_F(UserRepositoryRemoveTest, AnotherForeignKeyIsNotReportedAsBillingHistory) {
    exec("CREATE TABLE user_remove_test_ref (user_id uuid NOT NULL REFERENCES users (id) ON DELETE RESTRICT)");
    exec(std::string("INSERT INTO user_remove_test_ref VALUES ('") + kLeaver + "')");
    try {
        Repositories::UserRepository().remove(kLeaver);
        FAIL() << "the delete must not succeed";
    } catch (const Repositories::UserHasBillingHistory&) {
        FAIL() << "a table missing from kOwnedTables is a bug, not billing history";
    } catch (const pqxx::sql_error& e) {
        // 23503 up to PostgreSQL 17, 23001 (restrict_violation) from 18 on.
        EXPECT_TRUE(Repositories::detail::is_fk_violation(e.sqlstate())) << e.sqlstate();
    }
}

TEST(UserRepositoryBilling, OnlyTheBillingConstraintsCount) {
    using Repositories::UserRepository;
    EXPECT_TRUE(UserRepository::is_billing_violation(
        "ERROR:  update or delete on table \"users\" violates foreign key constraint \"payments_user_id_fkey\" on "
        "table \"payments\""));
    EXPECT_TRUE(UserRepository::is_billing_violation("constraint \"wallet_entries_user_id_fkey\""));
    EXPECT_TRUE(UserRepository::is_billing_violation("constraint \"wallet_balances_user_id_fkey\""));
    EXPECT_FALSE(UserRepository::is_billing_violation("constraint \"api_keys_user_id_fkey\""));
    EXPECT_FALSE(UserRepository::is_billing_violation(""));
}
