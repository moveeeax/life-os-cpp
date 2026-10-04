/**
 * @file test_xiaomi_seed.cpp
 * @brief Seeding Xiaomi credentials from the environment at service start.
 *
 * The cluster Secret is only a seed: if a rotated token is already in the
 * database, the environment values do not overwrite it. Otherwise every pod
 * restart would roll the token back to a stale one, and the first login would fail.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/fitness/CredentialsRepository.hpp"
#include "test_helpers.hpp"

namespace {

constexpr const char* kTestKeyB64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

class XiaomiSeedTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "xiaomi_seed_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        cfg["fitness"]["xiaomi"]["token_key"] = kTestKeyB64;
        cfg["fitness"]["xiaomi"]["user_id"] = "1234567890";
        cfg["fitness"]["xiaomi"]["pass_token"] = std::string(347, 'S');
        cfg["fitness"]["xiaomi"]["region"] = "cn";
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE xiaomi_credentials");
            return true;
        });
    }

    Repositories::CredentialsRepository repo() { return Repositories::CredentialsRepository(kTestKeyB64); }
};

}  // namespace

TEST_F(XiaomiSeedTest, SeedsEmptyTableFromConfig) {
    EXPECT_TRUE(Repositories::seed_xiaomi_credentials_if_missing());

    const auto loaded = repo().load();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->user_id, "1234567890");
    EXPECT_EQ(loaded->pass_token, std::string(347, 'S'));
    EXPECT_EQ(loaded->region, "cn");
}

// The rotated token in the database wins over the seed: Xiaomi issued it after
// the Secret was created, and overwriting would roll the session back to a dead value.
TEST_F(XiaomiSeedTest, DoesNotOverwriteARotatedToken) {
    repo().store({"1234567890", std::string(347, 'R'), "cn"});

    EXPECT_FALSE(Repositories::seed_xiaomi_credentials_if_missing());
    EXPECT_EQ(repo().load()->pass_token, std::string(347, 'R'));
}

TEST_F(XiaomiSeedTest, SeedIsIdempotent) {
    EXPECT_TRUE(Repositories::seed_xiaomi_credentials_if_missing());
    EXPECT_FALSE(Repositories::seed_xiaomi_credentials_if_missing());
    EXPECT_TRUE(repo().load().has_value());
}

namespace {

// Separate fixture: reseed=true is the emergency lever. The token in the database
// is dead (the cloud rejected it), the owner puts a fresh one into the Secret
// and enables the flag.
class XiaomiReseedTest : public XiaomiSeedTest {
protected:
    std::string config_file_name() const override { return "xiaomi_reseed_test_config.json"; }

    void config_overrides(nlohmann::json& cfg) override {
        XiaomiSeedTest::config_overrides(cfg);
        cfg["fitness"]["xiaomi"]["reseed"] = true;
    }
};

}  // namespace

TEST_F(XiaomiReseedTest, ReseedOverwritesTheStoredToken) {
    repo().store({"1234567890", std::string(347, 'R'), "cn"});

    EXPECT_TRUE(Repositories::seed_xiaomi_credentials_if_missing());
    EXPECT_EQ(repo().load()->pass_token, std::string(347, 'S'));
}
