/**
 * @file test_ani_level_api.cpp
 * @brief GET /api/v1/ani/level against a real database: the points the rules
 *        of domain/AniLevel.hpp give for rows of a closed day, the start day kept
 *        after the first read, a bad zone, two users apart.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AniLevelController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa7";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbb7";

using TestHelpers::body_of;
using TestHelpers::user;

class AniLevelApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::AniLevelController controller;
    std::string through;  // the last closed UTC day, as the route counts it

    std::string config_file_name() const override { return "ani_level_api_test_config.json"; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([&](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
                for (const char* table : {"ani_level_start",
                                          "food_entries",
                                          "food_water",
                                          "food_goals",
                                          "day_focus",
                                          "focus_sessions",
                                          "task_items"}) {
                    txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid",
                                    std::string(id));
                }
            }
            through = txn.exec("SELECT (((now() AT TIME ZONE 'UTC') - interval '4 hours')::date - 1)::text")[0][0]
                          .template as<std::string>();
            return true;
        });
    }

    /// The count starts @p days_before the last closed day.
    void start(const char* owner, int days_before) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("INSERT INTO ani_level_start (owner_id, started_on) VALUES ($1::uuid, $2::date - $3::int)",
                            std::string(owner),
                            through,
                            days_before);
            return true;
        });
    }

    json get(const char* owner, const std::string& tz = "UTC", int expect = 200) {
        auto req = TestHelpers::authed(user(owner), Get);
        req->setParameter("tz", tz);
        HttpResponsePtr captured;
        controller.get(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        EXPECT_EQ(static_cast<int>(captured->statusCode()), expect) << captured->body();
        if (expect == 200)
            TestHelpers::expect_2xx_matches_schema(captured, Get, "/api/v1/ani/level");
        return body_of(captured);
    }

    static json scale(const json& answer, const std::string& id) {
        for (const auto& s : answer["data"]["scales"])
            if (s["id"] == id)
                return s;
        return json();
    }
};

}  // namespace

TEST_F(AniLevelApiTest, PointsComeFromTheRowsOfAClosedDay) {
    start(kAnna, 1);
    Database::get().execute_write([&](auto& txn) {
        for (int i = 0; i < 3; ++i) {
            txn.exec_params(
                "INSERT INTO food_entries (owner_id, date, meal, name, kcal, protein_g) "
                "VALUES ($1::uuid, $2::date, 'lunch', 'Rice', 300, 10)",
                std::string(kAnna),
                through);
        }
        txn.exec_params("INSERT INTO food_water (owner_id, day, ml) VALUES ($1::uuid, $2::date, 2000)",
                        std::string(kAnna),
                        through);
        txn.exec_params(
            "INSERT INTO day_focus (owner_id, date, kind, text, done_at) "
            "VALUES ($1::uuid, $2::date, 'text', 'Write', now())",
            std::string(kAnna),
            through);
        txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, due, status, completed_at) "
            "VALUES ($1::uuid, 'Pay', 'finance', $2::date, 'done', ($2::date + time '12:00') AT TIME ZONE 'UTC')",
            std::string(kAnna),
            through);
        return true;
    });
    const json a = get(kAnna);
    EXPECT_EQ(a["data"]["through"], through);
    EXPECT_EQ(a["data"]["started_on"].get<std::string>() < through, true);
    EXPECT_EQ(scale(a, "food")["points"], 40 + 15)
        << "three entries and the water; no profile, no kcal or protein goal";
    EXPECT_EQ(scale(a, "tasks")["points"], 30 + 30 + 10) << "the day's focus, the task closed on time, nothing overdue";
    EXPECT_EQ(scale(a, "money")["points"], 2 * 20) << "no budgets: only the categorized part, both days";
    EXPECT_EQ(scale(a, "body")["points"], 0);
    EXPECT_EQ(a["data"]["weakest"], "body");
    EXPECT_EQ(a["data"]["level"], 1);
    EXPECT_EQ(scale(a, "food")["to"], 150);
}

TEST_F(AniLevelApiTest, ALevelIsTheLowestScalesAndThePointsOnlyGrow) {
    start(kAnna, 30);
    const json before = get(kAnna);
    EXPECT_EQ(before["data"]["level"], 1);
    Database::get().execute_write([&](auto& txn) {
        for (int i = 0; i < 3; ++i) {
            txn.exec_params(
                "INSERT INTO food_entries (owner_id, date, meal, name, kcal) VALUES ($1::uuid, $2::date, 'lunch', "
                "'Rice', 300)",
                std::string(kAnna),
                through);
        }
        return true;
    });
    const json after = get(kAnna);
    EXPECT_EQ(scale(after, "food")["points"].get<long>() - scale(before, "food")["points"].get<long>(), 40)
        << "a written day is not under the food debt, which would have halved it";
}

TEST_F(AniLevelApiTest, TheStartDayIsMadeOnTheFirstReadAndKept) {
    const json first = get(kAnna);
    const json second = get(kAnna);
    EXPECT_EQ(first["data"]["started_on"], second["data"]["started_on"]);
    EXPECT_EQ(Database::get().execute_read([&](auto& txn) {
        return txn
            .exec_params(
                "SELECT started_on = (now() AT TIME ZONE 'UTC')::date - 90 FROM ani_level_start "
                "WHERE owner_id = $1::uuid",
                std::string(kAnna))[0][0]
            .template as<bool>();
    }),
              true)
        << "90 days before the day of the first read";
}

TEST_F(AniLevelApiTest, TwoUsersAreCountedApart) {
    start(kAnna, 1);
    start(kBoris, 1);
    Database::get().execute_write([&](auto& txn) {
        for (int i = 0; i < 3; ++i) {
            txn.exec_params(
                "INSERT INTO food_entries (owner_id, date, meal, name, kcal) VALUES ($1::uuid, $2::date, 'lunch', "
                "'Rice', 300)",
                std::string(kBoris),
                through);
        }
        return true;
    });
    EXPECT_EQ(scale(get(kAnna), "food")["points"], 0);
    EXPECT_EQ(scale(get(kBoris), "food")["points"], 40);
}

TEST_F(AniLevelApiTest, ABadZoneIsRefused) {
    get(kAnna, "Mars/Olympus", 400);
}
