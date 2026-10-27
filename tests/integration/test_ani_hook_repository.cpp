/**
 * @file test_ani_hook_repository.cpp
 * @brief Ani's webhook rows against a real database (migration 062): the
 *        token stays until replaced, a sender's id is taken once a day, the
 *        page claims ready items oldest first and once, events link to one
 *        turn and end ready, silent or failed, old rows go, and a webhook
 *        turn does not lock the user's chat. And the bare switch.
 */

#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/agent/HookRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::HookRepository;
using Repositories::Agent::InboxRepository;
using Repositories::Agent::PrefsRepository;
using Repositories::Agent::TurnRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaac6";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbd6";

class AniHookRepository : public TestHelpers::CoreBackedTest {
protected:
    HookRepository hooks;
    InboxRepository inbox;
    TurnRepository turns;

    std::string config_file_name() const override { return "ani_hook_repository_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_inbox");
            txn.exec("DELETE FROM ani_hooks");
            txn.exec("DELETE FROM ani_prefs");
            txn.exec("DELETE FROM ani_actions");
            txn.exec("DELETE FROM ani_turns");
            return true;
        });
    }

    std::string add(const char* owner,
                    const std::string& kind,
                    const std::string& status,
                    const json& payload = json::object(),
                    const std::optional<std::string>& source_id = std::nullopt) {
        return inbox.add(owner, kind, payload, source_id, 86400, status).id;
    }

    /// Moves an inbox row's times back by @p seconds.
    static void age(const std::string& id, int seconds) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE ani_inbox SET created_at = created_at - make_interval(secs => $2), "
                "ready_at = ready_at - make_interval(secs => $2), delivered_at = delivered_at - make_interval(secs "
                "=> $2), expires_at = expires_at - make_interval(secs => $2) WHERE id::text = $1",
                id,
                seconds);
            return true;
        });
    }

    static std::string status_of(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT status FROM ani_inbox WHERE id::text = $1", id);
            return r.empty() ? std::string("gone") : r[0][0].template as<std::string>();
        });
    }
};

}  // namespace

TEST_F(AniHookRepository, TokenIsStableUntilRotated) {
    const std::string first = hooks.ensure(kAnna);
    EXPECT_TRUE(std::regex_match(first, std::regex("aniwh_[0-9a-f]{48}")));
    EXPECT_EQ(hooks.ensure(kAnna), first) << "the same URL every time";
    EXPECT_NE(hooks.ensure(kBoris), first);
    EXPECT_EQ(hooks.owner_of(first).value_or(""), kAnna);

    const std::string next = hooks.rotate(kAnna);
    EXPECT_NE(next, first);
    EXPECT_FALSE(hooks.owner_of(first).has_value()) << "the old URL stops at once";
    EXPECT_EQ(hooks.owner_of(next).value_or(""), kAnna);
    EXPECT_EQ(hooks.ensure(kAnna), next);
    EXPECT_FALSE(hooks.owner_of("aniwh_nope").has_value());
}

TEST_F(AniHookRepository, DuplicateSourceIdWithin24h) {
    const auto a = inbox.add(kAnna, "say", json{{"text", "Привет"}}, "m-1", 86400, "ready");
    EXPECT_FALSE(a.duplicate);
    const auto b = inbox.add(kAnna, "say", json{{"text", "Привет"}}, "m-1", 86400, "ready");
    EXPECT_TRUE(b.duplicate);
    EXPECT_EQ(b.id, a.id) << "the first item's id";
    EXPECT_FALSE(inbox.add(kBoris, "say", json{{"text", "Hi"}}, "m-1", 86400, "ready").duplicate) << "ids are per user";

    age(a.id, 25 * 3600);
    EXPECT_FALSE(inbox.add(kAnna, "say", json{{"text", "Снова"}}, "m-1", 86400, "ready").duplicate)
        << "a day later the id is free";
}

TEST_F(AniHookRepository, ClaimIsOldestFirstAndOnce) {
    const std::string one = add(kAnna, "say", "ready", json{{"text", "1"}});
    age(one, 30);
    const std::string two = add(kAnna, "say", "ready", json{{"text", "2"}});
    age(two, 20);
    const std::string three = add(kAnna, "say", "ready", json{{"text", "3"}});
    age(three, 10);
    add(kAnna, "event", "thinking");
    add(kBoris, "say", "ready");
    EXPECT_EQ(inbox.waiting(kAnna), 3) << "thinking items do not wait to be played yet";
    EXPECT_EQ(inbox.pending_count(kAnna), 4);

    const json first = inbox.claim(kAnna, 2);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0]["id"], one);
    EXPECT_EQ(first[1]["id"], two);
    const json second = inbox.claim(kAnna, 2);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second[0]["id"], three);
    EXPECT_TRUE(inbox.claim(kAnna, 2).empty());
    EXPECT_EQ(status_of(one), "delivered");
    EXPECT_EQ(inbox.waiting(kAnna), 0);
}

TEST_F(AniHookRepository, ExpiredItemsAreNotClaimed) {
    const std::string old = add(kAnna, "say", "ready");
    age(old, 90000);
    EXPECT_TRUE(inbox.claim(kAnna, 5).empty());
    EXPECT_EQ(inbox.waiting(kAnna), 0);
}

TEST_F(AniHookRepository, EventsLinkToATurnAndResolve) {
    const std::string a = add(kAnna, "event", "thinking", json{{"text", "a"}});
    age(a, 5);
    const std::string b = add(kAnna, "event", "thinking", json{{"text", "b"}});
    const json unlinked = inbox.thinking_unlinked(kAnna);
    ASSERT_EQ(unlinked.size(), 2u);
    EXPECT_EQ(unlinked[0]["id"], a) << "oldest first";

    const auto turn = turns.open(kAnna, "a\nb", "UTC", "hook");
    ASSERT_TRUE(turn.has_value());
    const std::string tid = (*turn)["id"];
    inbox.link({a, b}, tid);
    EXPECT_TRUE(inbox.thinking_unlinked(kAnna).empty());

    inbox.resolve_turn(tid, std::string("Маша пишет про созвон."));
    EXPECT_EQ(status_of(a), "ready");
    const json got = inbox.claim(kAnna, 5);
    ASSERT_EQ(got.size(), 1u) << "one line for the whole turn";
    EXPECT_EQ(got[0]["say"], "Маша пишет про созвон.");
    EXPECT_EQ(status_of(b), "delivered");
}

TEST_F(AniHookRepository, SilentAndFailedTurns) {
    const std::string a = add(kAnna, "event", "thinking");
    const std::string tid = (*turns.open(kAnna, "a", "UTC", "hook"))["id"];
    inbox.link({a}, tid);
    inbox.resolve_turn(tid, std::nullopt);
    EXPECT_EQ(status_of(a), "delivered") << "silence: nothing to play";
    EXPECT_TRUE(inbox.claim(kAnna, 5).empty());

    const std::string b = add(kAnna, "event", "thinking");
    turns.fail(tid, "failed");
    const std::string t2 = (*turns.open(kAnna, "b", "UTC", "hook"))["id"];
    inbox.link({b}, t2);
    inbox.fail_turn(t2);
    EXPECT_EQ(status_of(b), "failed");
    const json recent = inbox.recent(kAnna, 10);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_FALSE(recent[0].contains("payload")) << "no texts in the settings list";
    EXPECT_TRUE(recent[0].contains("kind"));
    EXPECT_TRUE(recent[0].contains("status"));
}

TEST_F(AniHookRepository, PurgeDropsOldDeliveredAndExpires) {
    const std::string gone = add(kAnna, "say", "ready");
    inbox.claim(kAnna, 5);
    age(gone, 8 * 86400);
    const std::string late = add(kAnna, "say", "ready");
    age(late, 2 * 86400);
    const std::string fresh = add(kAnna, "say", "ready");
    EXPECT_GE(inbox.purge(), 2);
    EXPECT_EQ(status_of(gone), "gone");
    EXPECT_EQ(status_of(late), "expired");
    EXPECT_EQ(status_of(fresh), "ready");
}

TEST_F(AniHookRepository, UserAndHookTurnsCanBeOpenTogether) {
    const auto hook = turns.open(kAnna, "event", "UTC", "hook");
    ASSERT_TRUE(hook.has_value());
    EXPECT_EQ((*hook)["origin"], "hook");
    EXPECT_FALSE(turns.open(kAnna, "event 2", "UTC", "hook").has_value()) << "one open hook turn";
    const auto user = turns.open(kAnna, "Привет", "UTC");
    ASSERT_TRUE(user.has_value()) << "the user is not held up by the webhook";
    EXPECT_EQ((*user)["origin"], "user");
    EXPECT_FALSE(turns.open(kAnna, "Ещё", "UTC").has_value()) << "one open user turn";

    EXPECT_FALSE(turns.running_other(kAnna, "user"));
    ASSERT_TRUE(turns.start((*hook)["id"].get<std::string>()));
    EXPECT_TRUE(turns.running_other(kAnna, "user")) << "the hook turn runs";
    EXPECT_FALSE(turns.running_other(kAnna, "hook"));

    const auto open_hook = turns.open_hook_turn(kAnna);
    ASSERT_TRUE(open_hook.has_value());
    EXPECT_EQ((*open_hook)["status"], "running");
    EXPECT_FALSE(turns.append_text((*hook)["id"].get<std::string>(), "more")) << "only while queued";
}

TEST_F(AniHookRepository, AppendTextWhileQueued) {
    const std::string id = (*turns.open(kAnna, "first", "UTC", "hook"))["id"];
    EXPECT_TRUE(turns.append_text(id, "second"));
    EXPECT_EQ((*turns.get(kAnna, id))["text"], "first\nsecond");
}

TEST_F(AniHookRepository, BareSwitch) {
    PrefsRepository prefs;
    EXPECT_EQ(prefs.get(kAnna), json({{"bare", false}}));
    prefs.set_bare(kAnna, true);
    EXPECT_EQ(prefs.get(kAnna), json({{"bare", true}}));
    EXPECT_EQ(prefs.get(kBoris), json({{"bare", false}}));
    prefs.set_bare(kAnna, false);
    EXPECT_EQ(prefs.get(kAnna), json({{"bare", false}}));
}
