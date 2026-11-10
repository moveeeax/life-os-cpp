/**
 * @file test_ani_api.cpp
 * @brief Ani's chat and agent routes as two users see them: Boris cannot read,
 *        delete, voice, confirm or cancel what is Anna's (404, and her rows
 *        stay), his wake and his clear touch only his own rows, and an API
 *        key gets 401 key_not_allowed on the page's own routes. The API runs
 *        without the upstream model key and the proxy URL, as in
 *        production, where only the ani worker holds them. And a meal or
 *        expenses she proposes: the worker writes them through the real food
 *        and money routes once, a second run or a declined one writes nothing.
 */

#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Actions.hpp"
#include "api/AniController.hpp"
#include "api/FoodController.hpp"
#include "api/MoneyController.hpp"
#include "database/Database.hpp"
#include "net/Http.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0a81";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0a82";

using Repositories::Agent::ActionRepository;
using Repositories::Agent::AgentRepository;
using Repositories::Agent::TurnRepository;
using TestHelpers::body_of;

class AniApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniController c;

    std::string config_file_name() const override { return "ani_api_test_config.json"; }
    void config_overrides(json& cfg) override {
        cfg["jobs"]["enabled"] = true;  // wake, messages and confirms queue the worker's jobs
        cfg["ani"]["agent"]["enabled"] = true;
        // No upstream_key and no proxy_url: the API pod does not get them.
        cfg["ani"]["agent"]["upstream_key"] = "";
        cfg["ani"]["agent"]["proxy_url"] = "";
        // The voice key only lets the speech route reach its owner check; no audio is fetched.
        cfg["now"]["ani_voice"]["api_key"] = "sk-voice-test";
        // A meal or expenses she proposes are written through these modules' routes.
        cfg["food"]["enabled"] = true;
        cfg["money"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* table : {"ani_actions", "ani_turns", "ani_usage", "ani_agents"}) {
                txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id IN ($1::uuid, $2::uuid)",
                                std::string(kAnna),
                                std::string(kBoris));
            }
            return true;
        });
    }

    using Handler = std::function<void(const HttpRequestPtr&, Api::AniController::Callback&&)>;

    static HttpResponsePtr call(const Security::Auth::AuthPrincipal& who,
                                HttpMethod method,
                                const json& body,
                                const Handler& fn) {
        auto req = TestHelpers::authed_json(who, body, method);
        // A handler may answer from another thread (the outbound pool).
        TestHelpers::AwaitedResponse answer;
        fn(req, answer.callback());
        const auto captured = answer.get();
        EXPECT_TRUE(captured);
        return captured;
    }

    static Security::Auth::AuthPrincipal key_of(const char* who) {
        auto p = TestHelpers::user(who);
        p.via_api_key = true;
        return p;
    }

    /// A pending proposal of Anna's on a turn of hers.
    static std::string annas_action(const std::string& note) {
        TurnRepository turns;
        auto turn = turns.open(kAnna, "Добавь " + note, "UTC");
        EXPECT_TRUE(turn.has_value());
        const std::string turn_id = (*turn)["id"];
        turns.start(turn_id);
        turns.finish(turn_id, "Предлагаю заметку", json::array());
        const json request = {{"method", "POST"}, {"path", "/api/v1/tasks/notes"}, {"body", {{"text", note}}}};
        return ActionRepository().add(kAnna, turn_id, "add_note", "Note to the inbox: «" + note + "»", request);
    }
};

}  // namespace

TEST_F(AniApi, TheApiWakesTheAgentWithoutTheUpstreamKey) {
    const auto woke =
        call(TestHelpers::user(kAnna), Post, json::object(), [&](auto req, auto cb) { c.wake(req, std::move(cb)); });
    ASSERT_EQ(woke->statusCode(), k202Accepted) << std::string(woke->body());
    TestHelpers::expect_2xx_matches_schema(woke, Post, "/api/v1/ani/agent/wake");
    EXPECT_EQ(body_of(woke)["data"]["state"], "starting");

    const auto boris =
        call(TestHelpers::user(kBoris), Get, json::object(), [&](auto req, auto cb) { c.agent(req, std::move(cb)); });
    ASSERT_EQ(boris->statusCode(), k200OK) << std::string(boris->body());
    EXPECT_EQ(body_of(boris)["data"]["state"], "stopped") << "Anna's wake is not Boris's agent";
    EXPECT_FALSE(AgentRepository().get(kBoris).has_value());
}

TEST_F(AniApi, AnotherUsersTurnIsNotFound) {
    const auto sent =
        call(TestHelpers::user(kAnna), Post, json{{"text", "Привет"}, {"tz", "UTC"}}, [&](auto req, auto cb) {
            c.message(req, std::move(cb));
        });
    ASSERT_EQ(sent->statusCode(), k202Accepted) << std::string(sent->body());
    TestHelpers::expect_2xx_matches_schema(sent, Post, "/api/v1/ani/messages");
    const std::string id = body_of(sent)["data"]["id"];

    const auto boris = TestHelpers::user(kBoris);
    const auto read = call(boris, Get, json::object(), [&](auto req, auto cb) { c.turn(req, std::move(cb), id); });
    EXPECT_EQ(read->statusCode(), k404NotFound) << std::string(read->body());
    EXPECT_EQ(body_of(read)["message"], "turn_not_found not found");
    const auto voiced = call(boris, Get, json::object(), [&](auto req, auto cb) { c.speech(req, std::move(cb), id); });
    EXPECT_EQ(voiced->statusCode(), k404NotFound) << std::string(voiced->body());
    const auto removed =
        call(boris, Delete, json::object(), [&](auto req, auto cb) { c.removeTurn(req, std::move(cb), id); });
    EXPECT_EQ(removed->statusCode(), k404NotFound) << std::string(removed->body());

    const auto list = call(boris, Get, json::object(), [&](auto req, auto cb) { c.turns(req, std::move(cb)); });
    ASSERT_EQ(list->statusCode(), k200OK) << std::string(list->body());
    EXPECT_TRUE(body_of(list)["data"].empty()) << "Boris's chat does not show Anna's turn";

    const auto cleared =
        call(boris, Delete, json::object(), [&](auto req, auto cb) { c.clearTurns(req, std::move(cb)); });
    EXPECT_EQ(cleared->statusCode(), k204NoContent) << std::string(cleared->body());
    EXPECT_TRUE(TurnRepository().get(kAnna, id).has_value()) << "clearing Boris's chat leaves Anna's turn";

    const auto own =
        call(TestHelpers::user(kAnna), Get, json::object(), [&](auto req, auto cb) { c.turn(req, std::move(cb), id); });
    EXPECT_EQ(own->statusCode(), k200OK) << std::string(own->body());
}

TEST_F(AniApi, AnotherUsersActionCannotBeConfirmedOrCancelled) {
    const std::string a = annas_action("молоко");
    const std::string b = annas_action("хлеб");
    const auto boris = TestHelpers::user(kBoris);

    const auto confirmed =
        call(boris, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), a); });
    EXPECT_EQ(confirmed->statusCode(), k404NotFound) << std::string(confirmed->body());
    EXPECT_EQ(body_of(confirmed)["message"], "action_not_found not found");
    const auto cancelled =
        call(boris, Post, json::object(), [&](auto req, auto cb) { c.cancelAction(req, std::move(cb), a); });
    EXPECT_EQ(cancelled->statusCode(), k404NotFound) << std::string(cancelled->body());
    EXPECT_EQ(ActionRepository().get(kAnna, a)->value("status", ""), "pending") << "Boris changed nothing";

    const auto anna = TestHelpers::user(kAnna);
    const auto ok =
        call(anna, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), a); });
    ASSERT_EQ(ok->statusCode(), k202Accepted) << std::string(ok->body());
    TestHelpers::expect_2xx_matches_schema(ok, Post, "/api/v1/ani/actions/{id}/confirm");
    const auto no = call(anna, Post, json::object(), [&](auto req, auto cb) { c.cancelAction(req, std::move(cb), b); });
    ASSERT_EQ(no->statusCode(), k200OK) << std::string(no->body());
    TestHelpers::expect_2xx_matches_schema(no, Post, "/api/v1/ani/actions/{id}/cancel");
    EXPECT_EQ(ActionRepository().get(kAnna, b)->value("status", ""), "cancelled");
}

TEST_F(AniApi, AnApiKeyIsRefusedOnThePagesRoutes) {
    const std::string a = annas_action("сыр");
    const auto key = key_of(kAnna);
    const auto sent = call(
        key, Post, json{{"text", "Привет"}, {"tz", "UTC"}}, [&](auto req, auto cb) { c.message(req, std::move(cb)); });
    EXPECT_EQ(sent->statusCode(), k401Unauthorized) << std::string(sent->body());
    EXPECT_EQ(body_of(sent)["error"], "key_not_allowed");
    const auto woke = call(key, Post, json::object(), [&](auto req, auto cb) { c.wake(req, std::move(cb)); });
    EXPECT_EQ(woke->statusCode(), k401Unauthorized) << std::string(woke->body());
    const auto confirmed =
        call(key, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), a); });
    EXPECT_EQ(confirmed->statusCode(), k401Unauthorized) << std::string(confirmed->body());

    EXPECT_EQ(ActionRepository().get(kAnna, a)->value("status", ""), "pending");
    EXPECT_FALSE(AgentRepository().get(kAnna).has_value()) << "the key woke nothing";
    EXPECT_EQ(TurnRepository().count_since(kAnna, 3600), 1) << "only the turn of the proposal";
}

namespace {

const std::string kSite = "http://api.test";

/// Answers the worker's requests with the real food and money routes, as Anna.
struct InProcess : Net::Http::Transport {
    Api::MoneyController money;
    Api::FoodController food;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("not expected");
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        throw std::runtime_error("not expected");
    }

    static std::string decoded(const std::string& v) {
        std::string out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '%' && i + 2 < v.size()) {
                out.push_back(static_cast<char>(std::stoi(v.substr(i + 1, 2), nullptr, 16)));
                i += 2;
            } else {
                out.push_back(v[i]);
            }
        }
        return out;
    }

    Net::Http::Response send(const Net::Http::Request& r) override {
        const std::string rest = r.url.substr(kSite.size());
        const auto mark = rest.find('?');
        const std::string path = rest.substr(0, mark);
        const auto who = TestHelpers::user(kAnna);
        HttpRequestPtr req;
        if (r.method == "GET") {
            req = TestHelpers::authed(who, Get);
            std::string query = mark == std::string::npos ? "" : rest.substr(mark + 1);
            while (!query.empty()) {
                const auto amp = query.find('&');
                const std::string pair = query.substr(0, amp);
                req->setParameter(pair.substr(0, pair.find('=')), decoded(pair.substr(pair.find('=') + 1)));
                query = amp == std::string::npos ? "" : query.substr(amp + 1);
            }
        } else {
            req = TestHelpers::authed_json(who, json::parse(r.body), Post);
        }
        TestHelpers::AwaitedResponse answer;
        if (r.method == "GET" && path == "/api/v1/money/accounts") {
            money.listAccounts(req, answer.callback());
        } else if (r.method == "GET" && path == "/api/v1/money/categories") {
            money.listCategories(req, answer.callback());
        } else if (r.method == "GET" && path == "/api/v1/money/currencies") {
            money.listCurrencies(req, answer.callback());
        } else if (r.method == "GET" && path == "/api/v1/money/merchants") {
            money.merchants(req, answer.callback());
        } else if (path == "/api/v1/money/accounts") {
            money.createAccount(req, answer.callback());
        } else if (path == "/api/v1/money/categories") {
            money.createCategory(req, answer.callback());
        } else if (path == "/api/v1/money/transactions/batch") {
            money.createTransactions(req, answer.callback());
        } else if (path == "/api/v1/food/entries/batch") {
            food.createEntries(req, answer.callback());
        } else {
            return {404, R"({"error":"not_found"})"};
        }
        const auto res = answer.get();
        return res ? Net::Http::Response{static_cast<long>(res->statusCode()), std::string(res->body())}
                   : Net::Http::Response{500, "{}"};
    }

    json data(const std::string& method, const std::string& path, const json& body = json::object()) {
        Net::Http::Request r;
        r.method = method;
        r.url = kSite + path;
        r.body = body.dump();
        const auto res = send(r);
        EXPECT_LT(res.status, 300) << path << " " << res.body;
        return json::parse(res.body)["data"];
    }
};

long long rows_of(const char* table) {
    return Database::get().execute_read([&](auto& txn) {
        return txn
            .exec_params(std::string("SELECT count(*) FROM ") + table + " WHERE owner_id = $1::uuid",
                         std::string(kAnna))[0][0]
            .template as<long long>();
    });
}

}  // namespace

TEST_F(AniApi, AConfirmedMealOrSpendIsWrittenOnceAndADeclinedOneNever) {
    namespace A = ::Agent::Actions;
    Database::get().execute_write([](auto& txn) {
        txn.exec_params(
            "INSERT INTO users (id, email, confirmed, role_id) "
            "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
            std::string(kAnna),
            std::string(kAnna) + "@example.test");
        for (const char* table : {"food_entries",
                                  "money_transactions",
                                  "money_merchants",
                                  "money_categories",
                                  "money_accounts",
                                  "money_currencies"}) {
            txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", std::string(kAnna));
        }
        return true;
    });
    InProcess site;
    site.data("GET", "/api/v1/money/currencies");  // the first list seeds the owner's currencies
    const std::string account = site.data(
        "POST", "/api/v1/money/accounts", {{"name", "Kaspi Gold"}, {"currency", "KZT"}, {"opening_balance", 0}})["id"];
    const std::string taxi = site.data("POST", "/api/v1/money/categories", {{"name", "Такси"}})["id"];

    const auto propose = [&](const char* tool, const json& args) {
        auto p = A::propose(site, kSite, "jwt", "UTC", "2026-11-10", tool, args);
        EXPECT_TRUE(std::holds_alternative<A::Proposal>(p))
            << (std::holds_alternative<std::string>(p) ? std::get<std::string>(p) : "");
        return std::holds_alternative<A::Proposal>(p) ? std::get<A::Proposal>(p) : A::Proposal{};
    };
    const auto spend = propose("add_spend",
                               json{{"lines",
                                     json::array({json{{"amount", 1600},
                                                       {"currency", "KZT"},
                                                       {"name", "Такси"},
                                                       {"merchant", "Yandex Go"},
                                                       {"category_id", taxi}}})}});
    EXPECT_EQ(spend.summary, "Spend: Такси 1600 KZT (Kaspi Gold), 2026-11-10");
    EXPECT_EQ(spend.request["body"]["transactions"][0]["account_id"], account) << "the only KZT account";
    const json lunch = json::parse(R"({"meal":"lunch","lines":[
        {"name":"Плов","grams":350,"kcal":560,"protein_g":18,"fat_g":22,"carbs_g":70},
        {"name":"Чай","kcal":90,"protein_g":0,"fat_g":0,"carbs_g":22}]})");
    const auto meal = propose("add_food", lunch);
    const auto other = propose("add_food", lunch);

    TurnRepository turns;
    const std::string turn = (*turns.open(kAnna, "Обед и такси", "UTC"))["id"];
    turns.start(turn);
    turns.finish(turn, "Записать?", json::array());
    ActionRepository actions;
    const std::string s = actions.add(kAnna, turn, spend.kind, spend.summary, spend.request);
    const std::string m = actions.add(kAnna, turn, meal.kind, meal.summary, meal.request);
    const std::string o = actions.add(kAnna, turn, other.kind, other.summary, other.request);

    const auto anna = TestHelpers::user(kAnna);
    const auto shown = call(anna, Get, json::object(), [&](auto req, auto cb) { c.turn(req, std::move(cb), turn); });
    ASSERT_EQ(shown->statusCode(), k200OK) << std::string(shown->body());
    TestHelpers::expect_2xx_matches_schema(shown, Get, "/api/v1/ani/turns/{id}");
    EXPECT_EQ(body_of(shown)["data"]["actions"][0]["kind"], "add_spend");
    EXPECT_EQ(body_of(shown)["data"]["actions"][1]["kind"], "add_food");

    // The worker's steps of the job ani_action (process_action), with the site in process.
    const auto run = [&](const std::string& id) {
        if (!actions.start(id)) {
            return false;
        }
        actions.finish(id, A::execute(site, kSite, "jwt", (*actions.for_worker(id))["request"]));
        return true;
    };
    // Confirmed as the route does it; the job it queues is played by run().
    ASSERT_EQ(actions.answer(kAnna, s, true), ActionRepository::Answer::Done);
    ASSERT_EQ(actions.answer(kAnna, m, true), ActionRepository::Answer::Done);
    EXPECT_TRUE(run(s));
    EXPECT_TRUE(run(m));
    EXPECT_EQ(actions.get(kAnna, s)->value("status", ""), "done") << actions.get(kAnna, s)->dump();
    EXPECT_EQ(actions.get(kAnna, m)->value("status", ""), "done") << actions.get(kAnna, m)->dump();
    EXPECT_EQ(rows_of("money_transactions"), 1);
    EXPECT_EQ(rows_of("food_entries"), 2);

    const auto again =
        call(anna, Post, json::object(), [&](auto req, auto cb) { c.confirmAction(req, std::move(cb), s); });
    EXPECT_EQ(again->statusCode(), k409Conflict) << std::string(again->body());
    EXPECT_EQ(body_of(again)["error"], "action_answered");
    EXPECT_FALSE(run(s)) << "a second job of a done action writes nothing";
    EXPECT_FALSE(run(m));
    EXPECT_NE(A::execute(site, kSite, "jwt", spend.request), "")
        << "even the same request sent again: the ledger has its external ids";
    EXPECT_EQ(rows_of("money_transactions"), 1);
    EXPECT_EQ(rows_of("food_entries"), 2);

    const auto no = call(anna, Post, json::object(), [&](auto req, auto cb) { c.cancelAction(req, std::move(cb), o); });
    ASSERT_EQ(no->statusCode(), k200OK) << std::string(no->body());
    EXPECT_FALSE(run(o)) << "a declined meal is never run";
    EXPECT_EQ(rows_of("food_entries"), 2);
}
