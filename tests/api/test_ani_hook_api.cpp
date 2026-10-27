/**
 * @file test_ani_hook_api.cpp
 * @brief Ani's webhook from outside and the inbox the Now page plays from:
 *        an unknown and a replaced token get the same 404, items are taken
 *        (202) and wait ready, bare only while the user turned it on, a
 *        sender's id once a day, 120 calls an hour, 50 waiting items, an
 *        event only while the agent is on and within its budget (it waits
 *        thinking, with a job for the worker), the token in a header; the
 *        page claims ready items oldest first, at most 5, each once.
 */

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AniHookController.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/HookRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0a71";

using Repositories::Agent::HookRepository;
using Repositories::Agent::InboxRepository;
using TestHelpers::body_of;

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

class AniHookApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniHookController c;
    std::string token;
    bool agent_on = true;

    std::string config_file_name() const override { return "ani_hook_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["jobs"]["enabled"] = true;  // events queue the worker's jobs
        cfg["ani"]["agent"]["enabled"] = agent_on;
        cfg["ani"]["agent"]["upstream_key"] = "sk-test";
        cfg["ani"]["agent"]["proxy_url"] = "http://worker.test/v1";
        cfg["ani"]["agent"]["monthly_tokens"] = 1000;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* table : {"ani_inbox", "ani_hooks", "ani_prefs", "ani_usage"}) {
                txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", std::string(kAnna));
            }
            return true;
        });
        Cache::get().del(std::string("ani:hook:rate:") + kAnna);
        token = HookRepository().ensure(kAnna);
    }

    /// POST /api/v1/ani/hook/{token} from outside: no session.
    HttpResponsePtr hook(const std::string& body,
                         const std::string& content_type = "application/json",
                         const std::string& tok = "") {
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setPath("/api/v1/ani/hook/" + (tok.empty() ? token : tok));
        req->setBody(body);
        req->addHeader("Content-Type", content_type);
        HttpResponsePtr captured;
        c.ingest(
            req, [&](const HttpResponsePtr& r) { captured = r; }, tok.empty() ? token : tok);
        EXPECT_TRUE(captured);
        return captured;
    }
    HttpResponsePtr hook(const json& body) { return hook(body.dump()); }

    HttpResponsePtr session(HttpMethod method,
                            const std::function<void(const HttpRequestPtr&, Api::AniHookController::Callback&&)>& fn,
                            const char* spec_path) {
        auto req = TestHelpers::authed_json(principal(kAnna), json::object(), method);
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        if (captured) {
            TestHelpers::expect_2xx_matches_schema(captured, method, spec_path);
        }
        return captured;
    }
    json claim() {
        auto r = session(
            Post, [&](auto req, auto cb) { c.claim(req, std::move(cb)); }, "/api/v1/ani/inbox/claim");
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"];
    }
    int waiting() {
        auto r = session(
            Get, [&](auto req, auto cb) { c.count(req, std::move(cb)); }, "/api/v1/ani/inbox/count");
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"]["waiting"].get<int>();
    }

    static std::string status_of(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT status FROM ani_inbox WHERE id::text = $1", id);
            return r.empty() ? std::string("gone") : r[0][0].template as<std::string>();
        });
    }
};

class AniHookAgentOffApi : public AniHookApi {
protected:
    AniHookAgentOffApi() { agent_on = false; }
    std::string config_file_name() const override { return "ani_hook_agent_off_api_test_config.json"; }
};

}  // namespace

TEST_F(AniHookApi, UnknownAndRotatedTokens404Alike) {
    const std::string old = token;
    auto unknown =
        hook(json{{"type", "say"}, {"text", "Hi"}}.dump(), "application/json", "aniwh_" + std::string(48, '0'));
    HookRepository().rotate(kAnna);
    auto rotated = hook(json{{"type", "say"}, {"text", "Hi"}}.dump(), "application/json", old);
    EXPECT_EQ(unknown->statusCode(), k404NotFound);
    EXPECT_EQ(rotated->statusCode(), k404NotFound);
    EXPECT_EQ(std::string(unknown->body()), std::string(rotated->body())) << "the same answer for both";
    EXPECT_EQ(waiting(), 0);
}

TEST_F(AniHookApi, SayIsQueuedReady) {
    auto r = hook(json{{"type", "say"}, {"text", "Сборка прошла."}});
    ASSERT_EQ(r->statusCode(), k202Accepted) << std::string(r->body());
    const std::string id = body_of(r)["data"]["id"];
    EXPECT_EQ(status_of(id), "ready");
    EXPECT_EQ(waiting(), 1);
    const json items = claim();
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0]["kind"], "say");
    EXPECT_EQ(items[0]["say"], "Сборка прошла.");
    EXPECT_TRUE(claim().empty()) << "each item once";
}

TEST_F(AniHookApi, BadBodiesAreRefused) {
    EXPECT_EQ(hook(std::string("not json"))->statusCode(), k400BadRequest);
    EXPECT_EQ(hook(json{{"type", "act"}, {"name", "pole"}})->statusCode(), k422UnprocessableEntity);
    EXPECT_EQ(hook(std::string(9000, ' '))->statusCode(), k413RequestEntityTooLarge);
    EXPECT_EQ(waiting(), 0);
}

TEST_F(AniHookApi, BareOffRefused) {
    auto off = hook(json{{"type", "outfit"}, {"outfit", "bare"}});
    EXPECT_EQ(off->statusCode(), k422UnprocessableEntity) << std::string(off->body());
    EXPECT_EQ(body_of(off)["error"], "bare_off");
    Repositories::Agent::PrefsRepository().set_bare(kAnna, true);
    EXPECT_EQ(hook(json{{"type", "outfit"}, {"outfit", "bare"}})->statusCode(), k202Accepted);
}

TEST_F(AniHookApi, DuplicateId409) {
    auto first = hook(json{{"type", "say"}, {"text", "Привет"}, {"id", "tg-1"}});
    ASSERT_EQ(first->statusCode(), k202Accepted);
    auto again = hook(json{{"type", "say"}, {"text", "Привет"}, {"id", "tg-1"}});
    EXPECT_EQ(again->statusCode(), k409Conflict);
    EXPECT_EQ(body_of(again)["error"], "duplicate");
    EXPECT_EQ(body_of(again)["id"], body_of(first)["data"]["id"]);
    EXPECT_EQ(waiting(), 1);
}

TEST_F(AniHookApi, RateLimit429) {
    for (int i = 0; i < 120; ++i) {
        ASSERT_EQ(hook(std::string("bad"))->statusCode(), k400BadRequest) << i;
    }
    auto r = hook(json{{"type", "say"}, {"text", "Ещё"}});
    EXPECT_EQ(r->statusCode(), k429TooManyRequests);
    EXPECT_EQ(body_of(r)["error"], "rate_limited");
}

TEST_F(AniHookApi, Backlog429) {
    InboxRepository inbox;
    for (int i = 0; i < 50; ++i) {
        inbox.add(kAnna, "say", json{{"text", "x"}}, std::nullopt, 86400, "ready");
    }
    auto r = hook(json{{"type", "say"}, {"text", "one more"}});
    EXPECT_EQ(r->statusCode(), k429TooManyRequests);
    EXPECT_EQ(body_of(r)["error"], "backlog");
}

TEST_F(AniHookApi, EventThinksWithAJob) {
    const auto before = Jobs::get().queue_depth_by_type();
    auto r = hook(json{{"type", "event"}, {"source", "telegram"}, {"text", "Маша: созвон?"}});
    ASSERT_EQ(r->statusCode(), k202Accepted) << std::string(r->body());
    EXPECT_EQ(status_of(body_of(r)["data"]["id"]), "thinking");
    EXPECT_EQ(waiting(), 0) << "nothing to play until her agent answers";
    const auto after = Jobs::get().queue_depth_by_type();
    const auto count = [](const auto& m) {
        const auto it = m.find("ani_hook_event");
        return it == m.end() ? 0LL : static_cast<long long>(it->second);
    };
    EXPECT_EQ(count(after), count(before) + 1);
}

TEST_F(AniHookApi, PlainTextIsAnEvent) {
    auto r = hook("Маша: созвон через 10 минут?", "text/plain; charset=utf-8");
    ASSERT_EQ(r->statusCode(), k202Accepted) << std::string(r->body());
    EXPECT_EQ(status_of(body_of(r)["data"]["id"]), "thinking");
}

TEST_F(AniHookApi, EventBudget429) {
    Repositories::Agent::AgentRepository().add_usage(kAnna, 900, 200);
    auto r = hook(json{{"type", "event"}, {"text", "x"}});
    EXPECT_EQ(r->statusCode(), k429TooManyRequests);
    EXPECT_EQ(body_of(r)["error"], "agent_budget");
    EXPECT_EQ(hook(json{{"type", "say"}, {"text", "x"}})->statusCode(), k202Accepted) << "say does not use the agent";
}

TEST_F(AniHookAgentOffApi, EventAgentOff409) {
    auto r = hook(json{{"type", "event"}, {"text", "x"}});
    EXPECT_EQ(r->statusCode(), k409Conflict);
    EXPECT_EQ(body_of(r)["error"], "agent_off");
    EXPECT_EQ(hook(json{{"type", "act"}, {"name", "wave"}})->statusCode(), k202Accepted);
}

TEST_F(AniHookApi, HeaderTokenWorks) {
    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Post);
    req->setPath("/api/v1/ani/hook");
    req->setBody(json{{"type", "act"}, {"name", "wave"}}.dump());
    req->addHeader("Content-Type", "application/json");
    req->addHeader("X-Ani-Hook-Token", token);
    HttpResponsePtr r;
    c.ingestHeader(req, [&](const HttpResponsePtr& x) { r = x; });
    ASSERT_TRUE(r);
    EXPECT_EQ(r->statusCode(), k202Accepted) << std::string(r->body());

    auto none = HttpRequest::newHttpRequest();
    none->setMethod(Post);
    none->setPath("/api/v1/ani/hook");
    none->setBody("{}");
    HttpResponsePtr n;
    c.ingestHeader(none, [&](const HttpResponsePtr& x) { n = x; });
    ASSERT_TRUE(n);
    EXPECT_EQ(n->statusCode(), k404NotFound);
}

TEST_F(AniHookApi, ClaimReturnsReadyOnlyAndMax5) {
    InboxRepository inbox;
    inbox.add(kAnna, "event", json{{"text", "x"}}, std::nullopt, 86400, "thinking");
    for (int i = 0; i < 7; ++i) {
        inbox.add(kAnna, "say", json{{"text", std::to_string(i)}}, std::nullopt, 86400, "ready");
    }
    EXPECT_EQ(waiting(), 7);
    const json first = claim();
    ASSERT_EQ(first.size(), 5u);
    for (const auto& item : first) {
        EXPECT_EQ(item["kind"], "say") << "a thinking event is not handed out";
    }
    EXPECT_EQ(claim().size(), 2u);
    EXPECT_EQ(waiting(), 0);
}

TEST_F(AniHookApi, AnswerRoute) {
    auto made = hook(json{{"type", "ask"}, {"text", "Списать 2 790?"}, {"options", {"Да", "Нет"}}});
    ASSERT_EQ(made->statusCode(), k202Accepted) << std::string(made->body());
    const std::string id = body_of(made)["data"]["id"];
    const auto asked = claim();
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0]["payload"]["options"], json({"Да", "Нет"}));

    const auto answer = [&](const json& body, const Security::Auth::AuthPrincipal& who) {
        auto req = TestHelpers::authed_json(who, body, Post);
        HttpResponsePtr captured;
        c.answer(
            req, [&](const HttpResponsePtr& r) { captured = r; }, id);
        EXPECT_TRUE(captured);
        return captured;
    };
    auto key = principal(kAnna);
    key.via_api_key = true;
    EXPECT_EQ(answer({{"answer", "Да"}}, key)->statusCode(), k401Unauthorized) << "the page's own route";
    EXPECT_EQ(answer({{"answer", "Может"}}, principal(kAnna))->statusCode(), k422UnprocessableEntity);
    const auto ok = answer({{"answer", "Да"}}, principal(kAnna));
    EXPECT_EQ(ok->statusCode(), k200OK) << std::string(ok->body());
    TestHelpers::expect_2xx_matches_schema(ok, Post, "/api/v1/ani/inbox/{id}/answer");
    EXPECT_EQ(answer({{"answer", "Нет"}}, principal(kAnna))->statusCode(), k409Conflict);
}
