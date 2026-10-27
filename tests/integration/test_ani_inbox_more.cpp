/**
 * @file test_ani_inbox_more.cpp
 * @brief Ani's inbox beyond lines: a reminder waits until it is due, a hold
 *        keeps everything back, a question is answered once and its
 *        callback goes out once (also when it expires unanswered), and a
 *        callback to a private address is refused at intake.
 */

#include <optional>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "database/Database.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "test_helpers.hpp"
#include "webhooks/Webhooks.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::PrefsRepository;
namespace Intake = Agent::Intake;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaacc";

class AniInboxMore : public TestHelpers::CoreBackedTest {
protected:
    InboxRepository inbox;

    std::string config_file_name() const override { return "ani_inbox_more_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["jobs"]["enabled"] = true; }

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

    std::string accepted(const json& body) {
        const auto got = Intake::accept(kAnna, item(body), "webhook");
        EXPECT_TRUE(std::holds_alternative<Intake::Accepted>(got))
            << (std::holds_alternative<Intake::Refused>(got) ? std::get<Intake::Refused>(got).code : "");
        return std::holds_alternative<Intake::Accepted>(got) ? std::get<Intake::Accepted>(got).id : "";
    }

    static void sql(const std::string& q, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(q, id);
            return true;
        });
    }

    static long deliveries() {
        const auto depth = Jobs::get().queue_depth_by_type();
        const auto it = depth.find(Webhooks::kJobType);
        return it == depth.end() ? 0 : it->second;
    }
};

}  // namespace

TEST_F(AniInboxMore, RemindWaitsUntilDue) {
    const std::string id = accepted({{"type", "remind"}, {"text", "Встань"}, {"in_seconds", 120}});
    EXPECT_TRUE(inbox.claim(kAnna, 5).empty()) << "not before its time";
    EXPECT_EQ(inbox.waiting(kAnna), 0) << "the dot waits too";
    sql("UPDATE ani_inbox SET not_before = now() - interval '1 second' WHERE id::text = $1", id);
    const json got = inbox.claim(kAnna, 5);
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0]["kind"], "remind");
    EXPECT_EQ(got[0]["say"], "Встань");
    const bool lives_past_due = Database::get().execute_read([&](auto& txn) {
        return txn
            .exec_params("SELECT expires_at > not_before + interval '23 hours' FROM ani_inbox WHERE id::text = $1",
                         id)[0][0]
            .template as<bool>();
    });
    EXPECT_TRUE(lives_past_due) << "its time to wait counts from when it is due";
}

TEST_F(AniInboxMore, RemindInThePastIsRefused) {
    const auto got =
        Intake::accept(kAnna, item({{"type", "remind"}, {"text", "x"}, {"at", "2001-01-01T00:00:00Z"}}), "webhook");
    ASSERT_TRUE(std::holds_alternative<Intake::Refused>(got));
    EXPECT_EQ(std::get<Intake::Refused>(got).code, "invalid_when");
}

TEST_F(AniInboxMore, HoldStopsClaims) {
    accepted({{"type", "say"}, {"text", "Привет"}});
    accepted({{"type", "hold"}, {"minutes", 30}});
    EXPECT_TRUE(inbox.claim(kAnna, 5).empty()) << "held";
    EXPECT_EQ(inbox.waiting(kAnna), 1) << "still counted";
    EXPECT_TRUE(PrefsRepository().held_until(kAnna).has_value());
    accepted({{"type", "hold"}, {"minutes", 0}});
    EXPECT_FALSE(PrefsRepository().held_until(kAnna).has_value());
    EXPECT_EQ(inbox.claim(kAnna, 5).size(), 1u);
}

TEST_F(AniInboxMore, AnswerOnce) {
    const std::string id = accepted({{"type", "ask"}, {"text", "Списать?"}, {"callback", "https://example.com/cb"}});
    EXPECT_EQ(inbox.answer(kAnna, id, "Может"), InboxRepository::Answer::BadOption);
    const long before = deliveries();
    EXPECT_EQ(Intake::answer(kAnna, id, "Да"), InboxRepository::Answer::Ok);
    EXPECT_EQ(Intake::answer(kAnna, id, "Нет"), InboxRepository::Answer::Answered);
    EXPECT_EQ(deliveries() - before, 1) << "one callback";
    EXPECT_EQ(inbox.item(kAnna, id)->value("answer", ""), "Да");
    EXPECT_EQ(Intake::send_callbacks(), 0) << "not again";
}

TEST_F(AniInboxMore, ExpiredAskCallsBackOnce) {
    const std::string id = accepted({{"type", "ask"}, {"text", "?"}, {"callback", "https://example.com/cb"}});
    sql("UPDATE ani_inbox SET expires_at = now() - interval '1 second' WHERE id::text = $1", id);
    const long before = deliveries();
    EXPECT_EQ(Intake::send_callbacks(), 1);
    EXPECT_EQ(Intake::send_callbacks(), 0);
    EXPECT_EQ(deliveries() - before, 1);
}

TEST_F(AniInboxMore, PrivateCallbackRefused) {
    const auto got =
        Intake::accept(kAnna, item({{"type", "ask"}, {"text", "?"}, {"callback", "https://10.0.0.1/x"}}), "webhook");
    ASSERT_TRUE(std::holds_alternative<Intake::Refused>(got));
    EXPECT_EQ(std::get<Intake::Refused>(got).status, 400);
    EXPECT_EQ(std::get<Intake::Refused>(got).code, "bad_callback");
}
