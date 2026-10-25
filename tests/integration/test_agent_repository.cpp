/**
 * @file test_agent_repository.cpp
 * @brief The agent's rows against a real database: each state change happens
 *        once and only from its own state, the proxy token finds only a live
 *        run, the month's usage adds up, and a deleted row leaves its
 *        snapshots to the files sweep (migration 058).
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "test_helpers.hpp"

namespace {

using Repositories::Agent::AgentRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaac5";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbd5";

class AgentRepositoryTest : public TestHelpers::CoreBackedTest {
protected:
    AgentRepository agents;

    std::string config_file_name() const override { return "agent_repository_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_agents");
            txn.exec("DELETE FROM ani_usage");
            txn.exec("DELETE FROM private_file_purges WHERE object_key LIKE 'test/%'");
            return true;
        });
    }

    std::string state(const char* owner) { return agents.get(owner)->value("state", ""); }
};

}  // namespace

TEST_F(AgentRepositoryTest, ARunGoesThroughItsStatesOnce) {
    EXPECT_FALSE(agents.get(kAnna).has_value());
    EXPECT_TRUE(agents.begin_start(kAnna));
    EXPECT_FALSE(agents.begin_start(kAnna)) << "already starting";
    EXPECT_EQ(state(kAnna), "starting");
    EXPECT_EQ(agents.count_active(), 1);

    EXPECT_FALSE(agents.set_running(kAnna, "10.42.3.7")) << "no run yet: set_run comes first";
    EXPECT_TRUE(agents.set_run(kAnna, "ani-x", "gw", "hash-1", "test/private/a/ani/"));
    EXPECT_TRUE(agents.set_running(kAnna, "10.42.3.7"));
    EXPECT_FALSE(agents.set_running(kAnna, "10.42.3.8")) << "running already";
    EXPECT_EQ(agents.get(kAnna)->value("pod_ip", ""), "10.42.3.7");

    EXPECT_FALSE(agents.set_stopped(kAnna, 1)) << "a running agent stops through stopping";
    EXPECT_TRUE(agents.begin_stop(kAnna, "the pod ended"));
    EXPECT_FALSE(agents.begin_stop(kAnna)) << "stopping already";
    EXPECT_TRUE(agents.set_stopped(kAnna, 1));
    const auto row = *agents.get(kAnna);
    EXPECT_EQ(row.value("state", ""), "stopped");
    EXPECT_EQ(row.value("generation", 0), 1);
    EXPECT_EQ(row.value("last_error", ""), "the pod ended");
    EXPECT_TRUE(row["pod_name"].is_null());
    EXPECT_EQ(agents.count_active(), 0);

    EXPECT_TRUE(agents.begin_start(kAnna)) << "a stopped agent starts again";
    EXPECT_TRUE(agents.get(kAnna)->at("last_error").is_null()) << "a new start forgets the old failure";
}

TEST_F(AgentRepositoryTest, TheProxyTokenFindsOnlyALiveRun) {
    ASSERT_TRUE(agents.begin_start(kAnna));
    ASSERT_TRUE(agents.set_run(kAnna, "ani-x", "gw", "hash-a", "test/private/a/ani/"));
    EXPECT_EQ(agents.owner_by_proxy_token("hash-a").value_or(""), kAnna);
    EXPECT_FALSE(agents.owner_by_proxy_token("hash-b").has_value());
    ASSERT_TRUE(agents.begin_stop(kAnna));
    EXPECT_FALSE(agents.owner_by_proxy_token("hash-a").has_value()) << "a stopping run calls no model";
    ASSERT_TRUE(agents.set_stopped(kAnna, 0));
    EXPECT_FALSE(agents.owner_by_proxy_token("hash-a").has_value());
}

TEST_F(AgentRepositoryTest, UsageAddsUpPerUserAndMonth) {
    EXPECT_EQ(agents.month_usage(kAnna).value("requests", -1), 0);
    agents.add_usage(kAnna, 100, 20);
    agents.add_usage(kAnna, 50, 5);
    agents.add_usage(kBoris, 7, 1);
    const auto u = agents.month_usage(kAnna);
    EXPECT_EQ(u.value("requests", 0), 2);
    EXPECT_EQ(u.value("prompt_tokens", 0), 150);
    EXPECT_EQ(u.value("completion_tokens", 0), 25);
    EXPECT_EQ(u.value("month", "").size(), 7U) << "YYYY-MM";
}

TEST_F(AgentRepositoryTest, TouchMovesOnlyALiveAgent) {
    ASSERT_TRUE(agents.begin_start(kAnna));
    Database::get().execute_write([](auto& txn) {
        txn.exec("UPDATE ani_agents SET last_active_at = now() - interval '1 hour'");
        return true;
    });
    agents.touch(kAnna);
    EXPECT_LT(agents.get(kAnna)->value("idle_seconds", 9999), 60);
    agents.touch(kBoris);
    EXPECT_FALSE(agents.get(kBoris).has_value()) << "touch creates nothing";
}

TEST_F(AgentRepositoryTest, ADeletedRowQueuesItsSnapshots) {
    ASSERT_TRUE(agents.begin_start(kAnna));
    ASSERT_TRUE(agents.set_run(kAnna, "ani-x", "gw", "hash-a", "test/private/a/ani/"));
    ASSERT_TRUE(agents.begin_stop(kAnna));
    ASSERT_TRUE(agents.set_stopped(kAnna, 3));
    const auto keys = Database::get().execute_write([](auto& txn) {
        txn.exec_params("DELETE FROM ani_agents WHERE owner_id = $1::uuid", std::string(kAnna));
        std::string out;
        for (const auto& r : txn.exec("SELECT object_key FROM private_file_purges WHERE object_key LIKE 'test/%' "
                                      "ORDER BY object_key")) {
            out += r[0].template as<std::string>() + ";";
        }
        return out;
    });
    EXPECT_EQ(keys, "test/private/a/ani/state-2.tgz;test/private/a/ani/state-3.tgz;test/private/a/ani/state-4.tgz;");
}
