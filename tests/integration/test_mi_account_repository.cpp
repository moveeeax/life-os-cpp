/**
 * @file test_mi_account_repository.cpp
 * @brief mi_accounts: one Xiaomi account per user, sealed token, link status,
 *        unlink with and without the account's data.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "test_helpers.hpp"

namespace {

constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
constexpr const char* kOtherKeyB64 = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=";
constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

class MiAccountRepositoryTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "mi_account_repository_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE mi_accounts, sync_runs, sync_state, heart_rate_samples, body_measurements");
            txn.exec("TRUNCATE TABLE workout_sessions CASCADE");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params("INSERT INTO users (id, email, confirmed, role_id) "
                                "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                                "ON CONFLICT DO NOTHING",
                                std::string(id),
                                std::string(id) + "@example.test");
            }
            return true;
        });
    }

    static Xiaomi::Credentials creds(const std::string& xiaomi_id, char fill = 'S') {
        return {xiaomi_id, std::string(347, fill), "cn"};
    }

    static long count(const std::string& sql) {
        return Database::get().execute_read([&](auto& txn) { return txn.exec(sql)[0][0].template as<long>(); });
    }

    static void exec(const std::string& sql) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec(sql);
            return true;
        });
    }
};

}  // namespace

TEST_F(MiAccountRepositoryTest, EmptyMeansNoLink) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    EXPECT_FALSE(repo.load(kAnna).has_value());
    EXPECT_FALSE(repo.load_first().has_value());
    EXPECT_FALSE(repo.status(kAnna).has_value());
    EXPECT_FALSE(repo.first_region().has_value());
    EXPECT_FALSE(repo.unlink(kAnna, true));
}

TEST_F(MiAccountRepositoryTest, LinkRoundTripsAndSealsTheToken) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);

    const auto loaded = repo.load(kAnna);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->user_id, "1111111111");
    EXPECT_EQ(loaded->pass_token, std::string(347, 'S'));
    EXPECT_EQ(loaded->region, "cn");
    EXPECT_FALSE(repo.load(kBoris).has_value());

    const auto first = repo.load_first();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->first, kAnna);

    const std::string stored = Database::get().execute_read(
        [](auto& txn) { return txn.exec("SELECT pass_token_sealed FROM mi_accounts")[0][0].template as<std::string>(); });
    EXPECT_EQ(stored.find(std::string(20, 'S')), std::string::npos);

    const auto status = repo.status(kAnna);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ((*status)["status"], "ok");
    EXPECT_EQ((*status)["region_detected"], true);
    EXPECT_FALSE((*status)["last_ok_at"].is_null());
    EXPECT_TRUE((*status)["last_sync"].is_null());
}

TEST_F(MiAccountRepositoryTest, WrongKeyFailsLoudlyOnLoad) {
    Repositories::MiAccountRepository(kTestKeyB64).link(kAnna, creds("1111111111"), true);
    Repositories::MiAccountRepository reader(kOtherKeyB64);
    EXPECT_THROW(reader.load(kAnna), Xiaomi::MiFitnessAuthError);
}

TEST_F(MiAccountRepositoryTest, SecondUserCannotLinkTheSameXiaomiAccount) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    EXPECT_THROW(repo.link(kBoris, creds("1111111111"), true), Repositories::AccountLinkedElsewhere);
    EXPECT_FALSE(repo.load(kBoris).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM mi_accounts"), 1);
}

TEST_F(MiAccountRepositoryTest, RelinkOfAnotherXiaomiAccountIsRefused) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    EXPECT_THROW(repo.link(kAnna, creds("2222222222"), true), Repositories::DifferentAccount);
    EXPECT_EQ(repo.load(kAnna)->user_id, "1111111111");
}

TEST_F(MiAccountRepositoryTest, RelinkOfTheSameAccountReplacesTheTokenAndClearsReauth) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    repo.mark_reauth_required(kAnna, "upstream_auth");
    EXPECT_EQ((*repo.status(kAnna))["status"], "reauth_required");
    EXPECT_EQ((*repo.status(kAnna))["last_error"], "upstream_auth");

    repo.link(kAnna, creds("1111111111", 'T'), false);
    EXPECT_EQ(repo.load(kAnna)->pass_token, std::string(347, 'T'));
    const auto status = *repo.status(kAnna);
    EXPECT_EQ(status["status"], "ok");
    EXPECT_TRUE(status["last_error"].is_null());
    EXPECT_EQ(status["region_detected"], false);
    EXPECT_EQ(count("SELECT COUNT(*) FROM mi_accounts"), 1);
}

TEST_F(MiAccountRepositoryTest, StoreRotatedKeepsStatusAndOwner) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    repo.link(kBoris, creds("2222222222"), true);
    repo.mark_reauth_required(kAnna, "upstream_auth");

    repo.store_rotated(kAnna, creds("1111111111", 'R'));
    EXPECT_EQ(repo.load(kAnna)->pass_token, std::string(347, 'R'));
    EXPECT_EQ((*repo.status(kAnna))["status"], "reauth_required");
    EXPECT_EQ(repo.load(kBoris)->pass_token, std::string(347, 'S'));

    // A rotation that arrives after the link was removed does not bring it back.
    ASSERT_TRUE(repo.unlink(kAnna, false));
    repo.store_rotated(kAnna, creds("1111111111", 'Z'));
    EXPECT_FALSE(repo.load(kAnna).has_value());
}

TEST_F(MiAccountRepositoryTest, MarkOkClearsTheError) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    repo.mark_reauth_required(kAnna, "upstream_auth");
    repo.mark_ok(kAnna);
    EXPECT_EQ((*repo.status(kAnna))["status"], "ok");
    EXPECT_TRUE((*repo.status(kAnna))["last_error"].is_null());
}

TEST_F(MiAccountRepositoryTest, RegionIsSetOnlyToAKnownCandidate) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), false);
    EXPECT_TRUE(repo.set_region(kAnna, "sg", true));
    EXPECT_EQ(repo.region_of(kAnna).value_or(""), "sg");
    EXPECT_EQ(repo.first_region().value_or(""), "sg");
    EXPECT_EQ((*repo.status(kAnna))["region_detected"], true);
    EXPECT_THROW(repo.set_region(kAnna, "evil.example", true), Xiaomi::MiFitnessAuthError);
    EXPECT_FALSE(repo.set_region(kBoris, "sg", true));
}

TEST_F(MiAccountRepositoryTest, StatusShowsTheLastFinishedSyncOfTheAccount) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    exec("INSERT INTO sync_runs (status, finished_at, xiaomi_user_id) VALUES "
         "('succeeded', now() - interval '2 hours', '1111111111'), "
         "('failed', now() - interval '1 hour', '1111111111'), "
         "('running', NULL, '1111111111'), "
         "('succeeded', now(), '2222222222')");
    const auto status = *repo.status(kAnna);
    ASSERT_FALSE(status["last_sync"].is_null());
    EXPECT_EQ(status["last_sync"]["status"], "failed");
}

TEST_F(MiAccountRepositoryTest, UnlinkKeepsDataByDefault) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    exec("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES ('1111111111', now(), 60, 'passive')");

    EXPECT_TRUE(repo.unlink(kAnna, false));
    EXPECT_FALSE(repo.load(kAnna).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM heart_rate_samples"), 1);
}

TEST_F(MiAccountRepositoryTest, UnlinkWithDataRemovesOnlyThisAccountsRows) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    repo.link(kAnna, creds("1111111111"), true);
    repo.link(kBoris, creds("2222222222"), true);
    exec("INSERT INTO heart_rate_samples (user_id, timestamp, bpm, sample_type) VALUES "
         "('1111111111', now(), 60, 'passive'), ('2222222222', now(), 70, 'passive')");
    exec("INSERT INTO body_measurements (user_id, timestamp, weight_kg) VALUES ('1111111111', now(), 80)");
    exec("INSERT INTO sync_runs (status, finished_at, xiaomi_user_id) VALUES "
         "('succeeded', now(), '1111111111'), ('succeeded', now(), '2222222222')");
    exec(std::string("INSERT INTO workout_sessions (owner_id, started_at, finished_at, health_status, hr_avg, hr_samples) VALUES ('") +
         kAnna + "', now() - interval '1 hour', now(), 'matched', 120, 6), ('" + kBoris +
         "', now() - interval '1 hour', now(), 'matched', 130, 6)");

    EXPECT_TRUE(repo.unlink(kAnna, true));

    EXPECT_EQ(count("SELECT COUNT(*) FROM heart_rate_samples WHERE user_id = '1111111111'"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM heart_rate_samples WHERE user_id = '2222222222'"), 1);
    EXPECT_EQ(count("SELECT COUNT(*) FROM body_measurements"), 0);
    EXPECT_EQ(count("SELECT COUNT(*) FROM sync_runs"), 1);
    EXPECT_EQ(count(std::string("SELECT COUNT(*) FROM workout_sessions WHERE owner_id = '") + kAnna +
                    "' AND health_status = 'pending' AND hr_avg IS NULL AND hr_samples = 0"),
              1);
    EXPECT_EQ(count(std::string("SELECT COUNT(*) FROM workout_sessions WHERE owner_id = '") + kBoris +
                    "' AND health_status = 'matched' AND hr_avg = 130"),
              1);
    EXPECT_TRUE(repo.load(kBoris).has_value());
}

TEST_F(MiAccountRepositoryTest, RejectsHeaderInjectionAndUnknownRegion) {
    Repositories::MiAccountRepository repo(kTestKeyB64);
    EXPECT_THROW(repo.link(kAnna, {"123\r\nX-Evil: 1", std::string(347, 'S'), "cn"}, true), Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(repo.link(kAnna, {"1111111111", std::string(347, 'S'), "evil.example"}, true),
                 Xiaomi::MiFitnessAuthError);
    EXPECT_THROW(repo.link(kAnna, {"1111111111", "bad token;", "cn"}, true), Xiaomi::MiFitnessAuthError);
    EXPECT_EQ(count("SELECT COUNT(*) FROM mi_accounts"), 0);
}
