/**
 * @file test_ani_cues_api.cpp
 * @brief GET /api/v1/ani/cues and POST /api/v1/ani/cues/{id}/dismiss: the
 *        rules' cues of a past day from the food diary, the tape and the
 *        month of money; a dismissed cue gone for that day only; the agent's
 *        own cue (agent/CueTools.hpp) listed and dismissed; unknown ids,
 *        bad queries and API keys refused.
 */

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/CueTools.hpp"
#include "api/AniCuesController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0c11";
/// A day long past whatever the clock says: its rules read the whole day (dinner is due).
constexpr const char* kDay = "2020-03-02";
constexpr const char* kZone = "Asia/Almaty";  // UTC+6 in 2020

using TestHelpers::body_of;

Security::Auth::AuthPrincipal principal(bool via_key = false) {
    Security::Auth::AuthPrincipal p;
    p.subject = kAnna;
    p.via_api_key = via_key;
    p.raw_claims = json{{"sub", kAnna}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

void sql(const std::string& q) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec(q);
        return true;
    });
}

class AniCuesApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniCuesController c;

    std::string config_file_name() const override { return "ani_cues_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["food"]["enabled"] = true;
        cfg["fitness"]["enabled"] = true;
        cfg["money"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        const std::string me = std::string("'") + kAnna + "'";
        for (const char* table : {"ani_cue_dismissals",
                                  "ani_cues",
                                  "food_entries",
                                  "tape_measurements",
                                  "money_transactions",
                                  "money_accounts",
                                  "money_categories",
                                  "money_currencies"}) {
            sql(std::string("DELETE FROM ") + table + " WHERE owner_id = " + me);
        }
    }

    HttpResponsePtr list(const std::vector<std::pair<std::string, std::string>>& params, bool via_key = false) {
        auto req = TestHelpers::authed_json(principal(via_key), json::object(), Get);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        c.list(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        TestHelpers::expect_2xx_matches_schema(captured, Get, "/api/v1/ani/cues");
        return captured;
    }

    json cues(const std::string& day = kDay) {
        auto r = list({{"date", day}, {"tz", kZone}});
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"];
    }

    HttpResponsePtr dismiss(const std::string& id,
                            const std::vector<std::pair<std::string, std::string>>& params = {{"date", kDay},
                                                                                              {"tz", kZone}}) {
        auto req = TestHelpers::authed_json(principal(), json::object(), Post);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        c.dismiss(
            req, [&](const HttpResponsePtr& r) { captured = r; }, id);
        EXPECT_TRUE(captured);
        TestHelpers::expect_2xx_matches_schema(captured, Post, "/api/v1/ani/cues/{id}/dismiss");
        return captured;
    }

    static std::vector<std::string> ids(const json& data) {
        std::vector<std::string> out;
        for (const auto& c : data) {
            out.push_back(c["id"].get<std::string>());
        }
        return out;
    }

    static json by_id(const json& data, const std::string& id) {
        for (const auto& c : data) {
            if (c["id"] == id) {
                return c;
            }
        }
        return json();
    }
};

}  // namespace

TEST_F(AniCuesApi, AnEmptyPastDayAsksForDinnerAndTheFirstMeasurement) {
    const json d = cues();
    EXPECT_EQ(ids(d), (std::vector<std::string>{"meal-missing-dinner", "measure-stale"}));
    const json dinner = by_id(d, "meal-missing-dinner");
    EXPECT_EQ(dinner["screen"], "food");
    EXPECT_EQ(dinner["text"], "Ужин еще не записан. Запишем?");
    EXPECT_EQ(dinner["action"], (json{{"label", "Ужин"}, {"kind", "food"}, {"payload", {{"meal", "dinner"}}}}));
    EXPECT_EQ(dinner["expires_at"], "2020-03-02T18:00:00Z") << "the next local midnight in Almaty (UTC+6 then)";
    const json tape = by_id(d, "measure-stale");
    EXPECT_EQ(tape["screen"], "body");
    EXPECT_EQ(tape["text"], "Замеров еще нет. Сделаем первый?");
    EXPECT_EQ(tape["action"], (json{{"label", "Замер"}, {"kind", "measure"}}));
}

TEST_F(AniCuesApi, TheDaysDataQuietsAndShapesTheRules) {
    sql(std::string("INSERT INTO food_entries (owner_id, date, meal, name, kcal) VALUES ('") + kAnna + "', '" + kDay +
        "', 'dinner', 'Плов', 650)");
    sql(std::string("INSERT INTO tape_measurements (owner_id, date, waist_cm) VALUES ('") + kAnna +
        "', '2020-02-01', 82)");
    // A measurement after the day does not count for it.
    sql(std::string("INSERT INTO tape_measurements (owner_id, date, waist_cm) VALUES ('") + kAnna +
        "', '2020-03-05', 81)");
    const json d = cues();
    EXPECT_EQ(ids(d), (std::vector<std::string>{"measure-stale"})) << "dinner is written";
    EXPECT_EQ(d[0]["text"], "Талию не мерили 30 дней. Сделаем сегодня?");
}

TEST_F(AniCuesApi, ACategoryOverItsBudgetIsNamed) {
    const std::string me = kAnna;
    sql("INSERT INTO money_currencies (owner_id, code) VALUES ('" + me + "', 'KZT')");
    sql("WITH a AS (INSERT INTO money_accounts (owner_id, name, currency) VALUES ('" + me +
        "', 'Kaspi', 'KZT') RETURNING id), c AS (INSERT INTO money_categories (owner_id, name, kind, budget_max, "
        "budget_currency) VALUES ('" +
        me +
        "', 'Кафе', 'expense', 1000, 'KZT') RETURNING id) "
        "INSERT INTO money_transactions (owner_id, type, date, account_id, category_id, amount, name) SELECT '" +
        me + "', 'expense', '" + kDay + "', a.id, c.id, 4800, 'Кофе' FROM a, c");
    const json money = by_id(cues(), "budget-over");
    ASSERT_TRUE(money.is_object());
    EXPECT_EQ(money["screen"], "money");
    EXPECT_EQ(money["text"],
              "«Кафе» уже на 3\xE2\x80\xAF"
              "800 ₸ выше бюджета.");
    EXPECT_TRUE(money["action"].is_null());
}

TEST_F(AniCuesApi, ADismissedCueIsGoneForThatDayOnly) {
    const auto r = dismiss("meal-missing-dinner");
    EXPECT_EQ(r->statusCode(), k204NoContent) << std::string(r->body());
    EXPECT_EQ(dismiss("meal-missing-dinner")->statusCode(), k204NoContent) << "again is a no-op";
    EXPECT_EQ(ids(cues()), (std::vector<std::string>{"measure-stale"}));
    EXPECT_EQ(ids(cues("2020-03-03")), (std::vector<std::string>{"meal-missing-dinner", "measure-stale"}))
        << "the next day brings it back";
}

TEST_F(AniCuesApi, AnUnknownCueIsNotFound) {
    EXPECT_EQ(dismiss("plan-snack")->statusCode(), k404NotFound);
    EXPECT_EQ(dismiss("agent-0b9f4c3e-5d1a-4a7e-9c11-2f6f0d4b8a10")->statusCode(), k404NotFound);
}

TEST_F(AniCuesApi, TheAgentsOwnCueIsListedAndDismissed) {
    auto run =
        Agent::CueTools::runner(kAnna, "UTC", [](const std::string&, const json&) { return std::string("rest"); });
    EXPECT_EQ(run("today", json::object()), "rest") << "other tools pass through";
    EXPECT_EQ(json::parse(run("set_cue", json{{"screen", "kitchen"}, {"text", "x"}}))["error"], "invalid");
    EXPECT_EQ(
        json::parse(run("set_cue", json{{"screen", "money"}, {"text", "x"}, {"action", {{"kind", "spend"}}}}))["error"],
        "invalid")
        << "a button needs its label";
    const json first = json::parse(run("set_cue", json{{"screen", "money"}, {"text", "Первая"}}));
    const json set = json::parse(run("set_cue",
                                     json{{"screen", "money"},
                                          {"text", "Такси уже третий раз за день"},
                                          {"action", {{"label", "Записать"}, {"kind", "spend"}}}}));
    ASSERT_TRUE(set["set"].is_string()) << set.dump();
    const std::string id = set["set"].get<std::string>();
    EXPECT_NE(first["set"], set["set"]) << "a new cue on the screen gets a new id";

    auto r = list({{"tz", "UTC"}});
    ASSERT_EQ(r->statusCode(), k200OK);
    const json data = body_of(r)["data"];
    const json mine = by_id(data, id);
    ASSERT_TRUE(mine.is_object()) << data.dump();
    EXPECT_EQ(mine["screen"], "money");
    EXPECT_EQ(mine["text"], "Такси уже третий раз за день");
    EXPECT_EQ(mine["action"], (json{{"label", "Записать"}, {"kind", "spend"}}));
    EXPECT_EQ(data.back()["id"], id) << "hers come after the rules'";
    int on_money = 0;
    for (const auto& cue : data) {
        on_money += cue["id"].get<std::string>().rfind("agent-", 0) == 0 ? 1 : 0;
    }
    EXPECT_EQ(on_money, 1) << "one per screen: the first was replaced";

    EXPECT_EQ(dismiss(id, {{"tz", "UTC"}})->statusCode(), k204NoContent);
    EXPECT_TRUE(by_id(body_of(list({{"tz", "UTC"}}))["data"], id).is_null());

    EXPECT_EQ(json::parse(run("clear_cue", json{{"screen", "money"}}))["cleared"], true);
    EXPECT_EQ(json::parse(run("clear_cue", json{{"screen", "money"}}))["error"], "not_found");
}

TEST_F(AniCuesApi, ABadQueryOrAnApiKeyIsRefused) {
    EXPECT_EQ(list({{"tz", "Mars/Olympus"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(list({{"date", "2020-02-30"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(list({{"date", kDay}}, /*via_key=*/true)->statusCode(), k401Unauthorized);
}
