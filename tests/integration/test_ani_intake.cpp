/**
 * @file test_ani_intake.cpp
 * @brief Agent::Intake, shared by the webhook and the MCP, against a real
 *        database: an accepted item lands in the inbox marked with where it
 *        came from; a repeated id, bare while it is off and a full backlog
 *        are refused with the webhook's codes.
 */

#include <string>
#include <variant>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "database/Database.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::InboxRepository;
namespace Intake = Agent::Intake;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaac8";

class AniIntake : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "ani_intake_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_inbox");
            txn.exec("DELETE FROM ani_prefs");
            return true;
        });
    }

    static Agent::Hook::Item item(const json& body) {
        auto parsed = Agent::Hook::parse(body.dump(), "application/json");
        EXPECT_TRUE(std::holds_alternative<Agent::Hook::Item>(parsed)) << body.dump();
        return std::get<Agent::Hook::Item>(parsed);
    }

    static json row(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT status, payload::text FROM ani_inbox WHERE id::text = $1", id);
            return json{{"status", r[0][0].template as<std::string>()},
                        {"payload", json::parse(r[0][1].template as<std::string>())}};
        });
    }
};

}  // namespace

TEST_F(AniIntake, AcceptedItemIsMarkedWithItsWay) {
    const auto got = Intake::accept(kAnna, item({{"type", "say"}, {"text", "Привет"}}), "mcp");
    ASSERT_TRUE(std::holds_alternative<Intake::Accepted>(got));
    const json r = row(std::get<Intake::Accepted>(got).id);
    EXPECT_EQ(r["status"], "ready");
    EXPECT_EQ(r["payload"]["via"], "mcp");
    EXPECT_EQ(r["payload"]["text"], "Привет");
}

TEST_F(AniIntake, RepeatedIdIsRefused) {
    const json body{{"type", "act"}, {"name", "wave"}, {"id", "x-1"}};
    ASSERT_TRUE(std::holds_alternative<Intake::Accepted>(Intake::accept(kAnna, item(body), "mcp")));
    const auto again = Intake::accept(kAnna, item(body), "webhook");
    ASSERT_TRUE(std::holds_alternative<Intake::Refused>(again));
    EXPECT_EQ(std::get<Intake::Refused>(again).status, 409);
    EXPECT_EQ(std::get<Intake::Refused>(again).code, "duplicate");
}

TEST_F(AniIntake, BareWhileOffIsRefused) {
    const auto got = Intake::accept(kAnna, item({{"type", "outfit"}, {"outfit", "bare"}}), "mcp");
    ASSERT_TRUE(std::holds_alternative<Intake::Refused>(got));
    EXPECT_EQ(std::get<Intake::Refused>(got).status, 422);
    EXPECT_EQ(std::get<Intake::Refused>(got).code, "bare_off");
}

TEST_F(AniIntake, FullBacklogIsRefused) {
    InboxRepository inbox;
    for (int i = 0; i < Agent::Hook::kPendingMax; ++i)
        inbox.add(kAnna, "act", json{{"name", "wave"}}, std::nullopt, 86400, "ready");
    const auto got = Intake::accept(kAnna, item({{"type", "act"}, {"name", "wave"}}), "mcp");
    ASSERT_TRUE(std::holds_alternative<Intake::Refused>(got));
    EXPECT_EQ(std::get<Intake::Refused>(got).status, 429);
    EXPECT_EQ(std::get<Intake::Refused>(got).code, "backlog");
}
