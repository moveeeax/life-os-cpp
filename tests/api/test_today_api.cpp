/**
 * @file test_today_api.cpp
 * @brief GET /api/v1/today and the focus of the day: the digest of an empty
 *        user, query validation, a module switched off, a focus on a task or
 *        a goal of the caller only, ticking a task focus closing and
 *        reopening the task, a focus whose task is gone.
 */

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/TodayController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaae7";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbe7";
constexpr const char* kDay = "2026-10-12";

Security::Auth::AuthPrincipal user(const std::string& id) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

std::string insert_task(const std::string& owner, const std::string& title, const char* due, const char* status) {
    return Database::get().execute_write([&](auto& txn) {
        auto r = txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, due, status, completed_at) "
            "VALUES ($1::uuid, $2, 'growth', $3::date, $4, CASE WHEN $4 = 'done' THEN now() END) RETURNING id::text",
            owner,
            title,
            due == nullptr ? std::optional<std::string>() : std::optional<std::string>(due),
            std::string(status));
        return r[0][0].template as<std::string>();
    });
}

std::string insert_goal(const std::string& owner, const std::string& title) {
    return Database::get().execute_write([&](auto& txn) {
        auto r = txn.exec_params(
            "INSERT INTO goal_items (owner_id, title, area, kind, start_date, due, target_count) "
            "VALUES ($1::uuid, $2, 'growth', 'count', '2026-01-01', '2026-12-31', 12) RETURNING id::text",
            owner,
            title);
        return r[0][0].template as<std::string>();
    });
}

void insert_spend(const std::string& owner, const std::string& name, double amount) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("INSERT INTO money_currencies (owner_id, code) VALUES ($1::uuid, 'KZT') ON CONFLICT DO NOTHING",
                        owner);
        txn.exec_params(
            "WITH a AS (INSERT INTO money_accounts (owner_id, name, currency) VALUES ($1::uuid, 'Kaspi', 'KZT') "
            " RETURNING id), c AS (INSERT INTO money_categories (owner_id, name, kind) VALUES ($1::uuid, 'Food', "
            " 'expense') RETURNING id) "
            "INSERT INTO money_transactions (owner_id, type, date, account_id, category_id, amount, name) "
            "SELECT $1::uuid, 'expense', $2::date, a.id, c.id, $3, $4 FROM a, c",
            owner,
            std::string(kDay),
            amount,
            name);
        return true;
    });
}

std::string task_status(const std::string& id) {
    return Database::get().execute_read([&](auto& txn) {
        return txn.exec_params("SELECT status FROM task_items WHERE id = $1::uuid", id)[0][0]
            .template as<std::string>();
    });
}

class TodayApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::TodayController c;

    std::string config_file_name() const override { return "today_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["now"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
        cfg["goals"]["enabled"] = true;
        cfg["food"]["enabled"] = true;
        cfg["money"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE day_focus, task_parse_jobs, task_notes, task_items, goal_items CASCADE");
            txn.exec(
                "DELETE FROM money_transactions WHERE owner_id IN ('aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaae7', "
                "'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbe7')");
            txn.exec(
                "DELETE FROM money_accounts WHERE owner_id IN ('aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaae7', "
                "'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbe7')");
            txn.exec(
                "DELETE FROM money_categories WHERE owner_id IN ('aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaae7', "
                "'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbe7')");
            txn.exec(
                "DELETE FROM money_currencies WHERE owner_id IN ('aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaae7', "
                "'bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbe7')");
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
    }

    using Handler = void (Api::TodayController::*)(const HttpRequestPtr&,
                                                   std::function<void(const HttpResponsePtr&)>&&);

    HttpResponsePtr call(Handler h,
                         const std::string& who,
                         HttpMethod m,
                         const json& body = json::object(),
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(user(who), body, m);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (c.*h)(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    json today(const std::string& who = kAnna) {
        auto r = call(&Api::TodayController::today, who, Get, json::object(), {{"date", kDay}, {"tz", "Asia/Almaty"}});
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r)["data"];
    }

    HttpResponsePtr put_focus(const json& body, const std::string& who = kAnna) {
        return call(&Api::TodayController::putFocus, who, Put, body);
    }

    HttpResponsePtr tick(bool done, const std::string& who = kAnna) {
        return call(&Api::TodayController::focusDone, who, Post, json{{"date", kDay}, {"done", done}});
    }
};

class TodayGoalsOffTest : public TodayApiTest {
protected:
    std::string config_file_name() const override { return "today_goals_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        TodayApiTest::config_overrides(cfg);
        cfg["goals"]["enabled"] = false;
    }
};

}  // namespace

TEST_F(TodayApiTest, AnEmptyUserGetsAQuietDigest) {
    const json d = today();
    EXPECT_TRUE(d["focus"].is_null());
    EXPECT_EQ(d["errors"], json::array());
    EXPECT_EQ(d["tasks"]["late_count"], 0);
    EXPECT_TRUE(d["tasks"]["first_late"].is_null());
    EXPECT_EQ(d["goals"], json::array());
    EXPECT_FALSE(d.contains("money")) << "no rows, no report blocks";
}

TEST_F(TodayApiTest, TheDigestCarriesLateAndTodaysTasksAndGoals) {
    insert_task(kAnna, "Pay the rent", "2026-10-01", "open");
    insert_task(kAnna, "Call the bank", kDay, "open");
    insert_task(kBoris, "Not Anna's", "2026-10-01", "open");
    insert_goal(kAnna, "Read 12 books");
    const json d = today();
    EXPECT_EQ(d["tasks"]["late_count"], 1);
    EXPECT_EQ(d["tasks"]["first_late"]["title"], "Pay the rent");
    ASSERT_EQ(d["tasks"]["today"].size(), 1u);
    EXPECT_EQ(d["tasks"]["today"][0]["title"], "Call the bank");
    ASSERT_EQ(d["goals"].size(), 1u);
    EXPECT_EQ(d["goals"][0]["title"], "Read 12 books");
}

TEST_F(TodayApiTest, BadQueriesAre400) {
    EXPECT_EQ(call(&Api::TodayController::today, kAnna, Get, json::object(), {{"date", kDay}, {"tz", "Mars/Base"}})
                  ->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call(&Api::TodayController::today, kAnna, Get, json::object(), {{"date", "2026-02-30"}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(call(&Api::TodayController::today, kAnna, Get)->statusCode(), k200OK) << "no date: today in UTC";
}

TEST_F(TodayGoalsOffTest, AModuleSwitchedOffIsAbsent) {
    const json d = today();
    EXPECT_FALSE(d.contains("goals"));
    EXPECT_TRUE(d.contains("tasks"));
}

TEST_F(TodayApiTest, AFocusOnSomeoneElsesTaskIsRefused) {
    const std::string boris_task = insert_task(kBoris, "Boris's", kDay, "open");
    auto r = put_focus(json{{"date", kDay}, {"kind", "task"}, {"ref_id", boris_task}});
    EXPECT_EQ(r->statusCode(), k422UnprocessableEntity) << std::string(r->body());
    EXPECT_EQ(body_of(r)["error"], "focus_ref_not_found");
}

TEST_F(TodayApiTest, AFocusOnAGoalCopiesItsTitle) {
    const std::string goal = insert_goal(kAnna, "Read 12 books");
    auto r = put_focus(json{{"date", kDay}, {"kind", "goal"}, {"ref_id", goal}, {"text", "ignored"}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["text"], "Read 12 books");
    EXPECT_EQ(today()["focus"]["ref_id"], goal);
}

TEST_F(TodayApiTest, ATextFocusNeedsItsText) {
    EXPECT_EQ(put_focus(json{{"date", kDay}, {"kind", "text"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", std::string(201, 'x')}})->statusCode(),
              k400BadRequest);
    EXPECT_EQ(put_focus(json{{"date", kDay}, {"kind", "task"}})->statusCode(), k400BadRequest) << "no ref_id";
    EXPECT_EQ(put_focus(json{{"date", "someday"}, {"kind", "text"}, {"text", "x"}})->statusCode(), k400BadRequest);
    auto r = put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", "Ship Now"}});
    ASSERT_EQ(r->statusCode(), k200OK);
    EXPECT_EQ(body_of(r)["data"],
              (json{{"date", kDay},
                    {"kind", "text"},
                    {"ref_id", nullptr},
                    {"text", "Ship Now"},
                    {"done", false},
                    {"missing", false}}));
}

TEST_F(TodayApiTest, ACyrillicTitleAndTextAreMeasuredInCharacters) {
    std::string long_title;
    for (int i = 0; i < 150; ++i) {
        long_title += "я";  // two bytes each: 300 bytes, 150 characters
    }
    const std::string task = insert_task(kAnna, long_title, kDay, "open");
    auto r = put_focus(json{{"date", kDay}, {"kind", "task"}, {"ref_id", task}});
    ASSERT_EQ(r->statusCode(), k200OK) << "a byte cut would split a character: " << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["text"], long_title);

    r = put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", long_title}});
    ASSERT_EQ(r->statusCode(), k200OK) << "150 characters fit the 200 the table allows";
    EXPECT_EQ(body_of(r)["data"]["text"], long_title);
    std::string too_long;
    for (int i = 0; i < 201; ++i) {
        too_long += "я";
    }
    EXPECT_EQ(put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", too_long}})->statusCode(), k400BadRequest);
}

TEST_F(TodayApiTest, TickingATaskFocusClosesAndReopensTheTask) {
    const std::string t = insert_task(kAnna, "Call the bank", kDay, "open");
    ASSERT_EQ(put_focus(json{{"date", kDay}, {"kind", "task"}, {"ref_id", t}})->statusCode(), k200OK);

    auto r = tick(true);
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["done"], true);
    EXPECT_EQ(task_status(t), "done");

    EXPECT_EQ(tick(true)->statusCode(), k200OK) << "ticking twice is fine";
    EXPECT_EQ(task_status(t), "done");

    r = tick(false);
    ASSERT_EQ(r->statusCode(), k200OK);
    EXPECT_EQ(body_of(r)["data"]["done"], false);
    EXPECT_EQ(task_status(t), "open");
}

TEST_F(TodayApiTest, ATaskClosedElsewhereStaysClosedWhenTicked) {
    const std::string t = insert_task(kAnna, "Done already", kDay, "done");
    ASSERT_EQ(put_focus(json{{"date", kDay}, {"kind", "task"}, {"ref_id", t}})->statusCode(), k200OK);
    EXPECT_EQ(tick(true)->statusCode(), k200OK);
    EXPECT_EQ(task_status(t), "done");
}

TEST_F(TodayApiTest, AFocusWhoseTaskIsGoneIsMissing) {
    const std::string t = insert_task(kAnna, "Gone soon", kDay, "open");
    ASSERT_EQ(put_focus(json{{"date", kDay}, {"kind", "task"}, {"ref_id", t}})->statusCode(), k200OK);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("DELETE FROM task_items WHERE id = $1::uuid", t);
        return true;
    });
    const json f = today()["focus"];
    EXPECT_EQ(f["missing"], true);
    EXPECT_EQ(f["text"], "Gone soon");
    auto r = tick(true);
    EXPECT_EQ(r->statusCode(), k422UnprocessableEntity) << std::string(r->body());
}

TEST_F(TodayApiTest, PuttingAgainReplacesAndClearsDone) {
    ASSERT_EQ(put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", "One"}})->statusCode(), k200OK);
    ASSERT_EQ(tick(true)->statusCode(), k200OK);
    auto r = put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", "Two"}});
    EXPECT_EQ(body_of(r)["data"]["text"], "Two");
    EXPECT_EQ(body_of(r)["data"]["done"], false);

    auto del = call(&Api::TodayController::deleteFocus, kAnna, Delete, json::object(), {{"date", kDay}});
    EXPECT_EQ(del->statusCode(), k204NoContent);
    EXPECT_TRUE(today()["focus"].is_null());
    EXPECT_EQ(tick(true)->statusCode(), k404NotFound) << "no focus to tick";
}

TEST_F(TodayApiTest, EachUserHasTheirOwnFocus) {
    ASSERT_EQ(put_focus(json{{"date", kDay}, {"kind", "text"}, {"text", "Anna's"}})->statusCode(), k200OK);
    EXPECT_TRUE(today(kBoris)["focus"].is_null());
}

TEST_F(TodayApiTest, TheDigestCarriesTheCornerDetails) {
    insert_task(kAnna, "Pay the rent", "2026-10-01", "open");
    insert_task(kAnna, "Renew the insurance", "2026-10-05", "open");
    insert_goal(kAnna, "Read 12 books");
    insert_spend(kAnna, "Groceries", 5000);
    const json d = today();
    ASSERT_EQ(d["tasks"]["late"].size(), 2u);
    EXPECT_EQ(d["tasks"]["late"][0]["title"], "Pay the rent");
    EXPECT_TRUE(d["goals"][0].contains("days_left"));
    EXPECT_TRUE(d["goals"][0].contains("progress"));
    ASSERT_TRUE(d.contains("money")) << d.dump();
    ASSERT_EQ(d["money"]["spends_today"].size(), 1u);
    EXPECT_EQ(d["money"]["spends_today"][0]["name"], "Groceries");
    EXPECT_EQ(d["money"]["spends_today"][0]["currency"], "KZT");
    EXPECT_DOUBLE_EQ(d["money"]["spends_today"][0]["amount"].get<double>(), 5000);
    ASSERT_TRUE(d["money"]["currencies"].is_array());
    EXPECT_EQ(d["money"]["currencies"][0]["currency"], "KZT");
    ASSERT_TRUE(d["money"]["net_worth"].is_object()) << d.dump();
    EXPECT_EQ(d["money"]["net_worth"]["currency"], "USD");
    EXPECT_TRUE(d["money"]["net_worth"].contains("rate_date"));
    EXPECT_TRUE(d["money"]["net_worth"]["change_partial"].is_boolean());
}

TEST_F(TodayApiTest, ASpendOfSomeoneElseStaysTheirs) {
    insert_spend(kBoris, "Boris's lunch", 3000);
    EXPECT_FALSE(today()["money"].is_object() && !today()["money"]["spends_today"].empty());
}
