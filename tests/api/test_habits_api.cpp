/**
 * @file test_habits_api.cpp
 * @brief /api/v1/habits: the module switch, the checks of a habit body, the
 *        create-edit-archive-delete round, the owner scope on every id route,
 *        manual ticks (a day after today and a day too far back refused), the
 *        days with auto values from seeded Mi Fitness and Training rows on the
 *        local day, a source without data, the streak from ticks, the habits
 *        part of /api/v1/today, a daily target reached by adding to a day, and
 *        the mood check-in of a day.
 */

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/HabitsController.hpp"
#include "api/TodayController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "repositories/habits/Days.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaab37";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbb37";
constexpr const char* kAnnaMi = "habits-test-anna-mi";

Security::Auth::AuthPrincipal user(const char* who, std::uint32_t permissions) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", permissions}};
    return p;
}

Security::Auth::AuthPrincipal reader(const char* who) {
    return user(who, Domain::Permission::kGeneral | Domain::Permission::kFitnessRead);
}

using TestHelpers::body_of;

/// Today in UTC and a day @p shift days from it, as the server computes them (now() in the database).
std::string utc_day(int shift = 0) {
    return Database::get().execute_read([&](auto& txn) {
        return txn
            .exec_params("SELECT to_char((now() AT TIME ZONE 'UTC')::date + $1::integer, 'YYYY-MM-DD')", shift)[0][0]
            .template as<std::string>();
    });
}

void sql(const std::string& statement) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec(statement);
        return true;
    });
}

class HabitsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::HabitsController c;
    Api::TodayController today;

    std::string config_file_name() const override { return "habits_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["habits"]["enabled"] = true;
        cfg["fitness"]["enabled"] = true;
        cfg["workout"]["enabled"] = true;
        cfg["now"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM habits WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM workout_sessions WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM mi_accounts WHERE owner_id = $1::uuid", o);
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    o,
                    o + "@example.test");
            }
            const std::string mi = kAnnaMi;
            txn.exec_params("DELETE FROM daily_activity WHERE user_id = $1", mi);
            txn.exec_params("DELETE FROM sleep_sessions WHERE user_id = $1", mi);
            return true;
        });
    }

    using Fn = std::function<void(const HttpRequestPtr&, Api::HabitsController::Callback&&)>;

    HttpResponsePtr call(const Security::Auth::AuthPrincipal& who,
                         HttpMethod method,
                         const json& body,
                         const Fn& fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(who, body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr call(const char* who,
                         HttpMethod method,
                         const json& body,
                         const Fn& fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        return call(reader(who), method, body, fn, params);
    }

    std::string created(const char* who, const json& body) {
        auto r = call(who, Post, body, [&](auto req, auto cb) { c.create(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/habits");
        return body_of(r)["data"]["id"].get<std::string>();
    }

    json list(const char* who, const char* archived = "") {
        auto r = call(
            who, Get, json::object(), [&](auto req, auto cb) { c.list(req, std::move(cb)); }, {{"archived", archived}});
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/habits");
        return body_of(r)["data"];
    }

    json days(const Security::Auth::AuthPrincipal& who,
              const std::string& from,
              const std::string& to,
              const char* tz) {
        auto r = call(who,
                      Get,
                      json::object(),
                      [&](auto req, auto cb) { c.days(req, std::move(cb)); },
                      {{"from", from}, {"to", to}, {"tz", tz}});
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/habits/days");
        return body_of(r);
    }

    HttpResponsePtr tick(const char* who,
                         const std::string& id,
                         const std::string& date,
                         const json& body = json::object()) {
        auto r =
            call(who, Put, body, [&](auto req, auto cb) { c.putCheck(req, std::move(cb), id, date); }, {{"tz", "UTC"}});
        TestHelpers::expect_2xx_matches_schema(r, "PUT", "/api/v1/habits/{id}/checks/{date}");
        return r;
    }

    static json habit_of(const json& list, const std::string& id) {
        for (const auto& h : list) {
            if (h["id"] == id) {
                return h;
            }
        }
        ADD_FAILURE() << "no habit " << id;
        return json::object();
    }

    static json day_of(const json& habit, const std::string& date) {
        for (const auto& d : habit["days"]) {
            if (d["date"] == date) {
                return d;
            }
        }
        ADD_FAILURE() << "no day " << date;
        return json::object();
    }
};

class HabitsOffTest : public HabitsApiTest {
protected:
    std::string config_file_name() const override { return "habits_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        HabitsApiTest::config_overrides(cfg);
        cfg["habits"]["enabled"] = false;
    }
};

}  // namespace

TEST_F(HabitsOffTest, TheRoutesAreNotThereWhileTheModuleIsOff) {
    auto r = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.list(req, std::move(cb)); });
    EXPECT_EQ(r->statusCode(), k404NotFound);
}

TEST_F(HabitsApiTest, AHabitBodyIsChecked) {
    created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    created(kAnna, json{{"name", "Swimming"}, {"schedule", "weekly"}, {"per_week", 3}, {"goal", 45}, {"unit", "min"}});
    created(kAnna, json{{"name", "Sleep 8 h"}, {"schedule", "daily"}, {"source", "sleep"}, {"goal", 8}});
    for (const json& bad : {
             json{{"name", "Swimming"}, {"schedule", "weekly"}},                   // no per_week
             json{{"name", "Swimming"}, {"schedule", "weekly"}, {"per_week", 8}},  // more than 7
             json{{"name", "Reading"}, {"schedule", "daily"}, {"per_week", 3}},    // daily with per_week
             json{{"name", "Steps"}, {"schedule", "daily"}, {"source", "steps"}},  // a Mi source without a goal
             json{{"name", "Sleep"}, {"schedule", "daily"}, {"source", "sleep"}, {"goal", 25}},  // over 24 h
             json{{"name", "Reading"}, {"schedule", "daily"}, {"goal", 0}},        // a goal must be above 0
             json{{"name", "Water"}, {"schedule", "daily"}, {"source", "water"}},  // unknown source
             json{{"name", ""}, {"schedule", "daily"}},                            // empty name
             json{{"name", std::string(81, 'x')}, {"schedule", "daily"}},          // 81 characters
             json{{"name", "Reading"}, {"schedule", "daily"}, {"unit", std::string(17, 'u')}},
             json{{"name", "Reading"}, {"schedule", "monthly"}},
             json{{"schedule", "daily"}},
         }) {
        auto r = call(kAnna, Post, bad, [&](auto req, auto cb) { c.create(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k400BadRequest) << bad.dump();
    }
    // A workout is any finished session: its goal is dropped.
    auto w =
        call(kAnna,
             Post,
             json{{"name", "Workout"}, {"schedule", "weekly"}, {"per_week", 4}, {"source", "workout"}, {"goal", 3}},
             [&](auto req, auto cb) { c.create(req, std::move(cb)); });
    ASSERT_EQ(w->statusCode(), k201Created) << std::string(w->body());
    EXPECT_TRUE(body_of(w)["data"]["goal"].is_null());
    EXPECT_EQ(list(kAnna).size(), 4U);
}

TEST_F(HabitsApiTest, CreateEditArchiveDelete) {
    const auto a = created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    const auto b = created(kAnna, json{{"name", "Reading"}, {"schedule", "daily"}});
    auto rows = list(kAnna);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0]["name"], "Meditation") << "in the order created";
    EXPECT_LT(rows[0]["position"].get<int>(), rows[1]["position"].get<int>());
    EXPECT_EQ(rows[0]["source"], "manual");
    EXPECT_TRUE(rows[0]["per_week"].is_null());

    auto edited = call(kAnna,
                       Patch,
                       json{{"name", "Reading 20 min"}, {"schedule", "weekly"}, {"per_week", 5}},
                       [&](auto req, auto cb) { c.update(req, std::move(cb), b); });
    ASSERT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    TestHelpers::expect_matches_schema(edited, "PATCH", "/api/v1/habits/{id}");
    EXPECT_EQ(body_of(edited)["data"]["name"], "Reading 20 min");
    EXPECT_EQ(body_of(edited)["data"]["per_week"], 5);

    auto archived =
        call(kAnna, Post, json{{"archived", true}}, [&](auto req, auto cb) { c.archive(req, std::move(cb), a); });
    ASSERT_EQ(archived->statusCode(), k200OK);
    TestHelpers::expect_matches_schema(archived, "POST", "/api/v1/habits/{id}/archive");
    EXPECT_TRUE(body_of(archived)["data"]["archived_at"].is_string());
    EXPECT_EQ(list(kAnna).size(), 1U);
    ASSERT_EQ(list(kAnna, "true").size(), 1U);
    EXPECT_EQ(list(kAnna, "true")[0]["id"], a);
    EXPECT_EQ(call(kAnna, Post, json{{"archived", "yes"}}, [&](auto req, auto cb) { c.archive(req, std::move(cb), a); })
                  ->statusCode(),
              k400BadRequest);
    call(kAnna, Post, json{{"archived", false}}, [&](auto req, auto cb) { c.archive(req, std::move(cb), a); });
    EXPECT_EQ(list(kAnna).size(), 2U) << "restored";

    EXPECT_EQ(tick(kAnna, a, utc_day(-1))->statusCode(), k200OK);
    EXPECT_EQ(
        call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.remove(req, std::move(cb), a); })->statusCode(),
        k204NoContent);
    EXPECT_EQ(list(kAnna).size(), 1U);
    const long left = Database::get().execute_read([&](auto& txn) {
        return txn.exec_params("SELECT count(*) FROM habit_checks WHERE habit_id = $1::uuid", a)[0][0]
            .template as<long>();
    });
    EXPECT_EQ(left, 0) << "the ticks went with the habit";
}

TEST_F(HabitsApiTest, AnotherOwnersHabitIsNotFound) {
    const auto a = created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    const std::string yesterday = utc_day(-1);
    ASSERT_EQ(tick(kAnna, a, yesterday)->statusCode(), k200OK);
    const json edit{{"name", "Mine now"}, {"schedule", "daily"}};
    EXPECT_EQ(call(kBoris, Patch, edit, [&](auto req, auto cb) { c.update(req, std::move(cb), a); })->statusCode(),
              k404NotFound);
    EXPECT_EQ(
        call(kBoris, Delete, json::object(), [&](auto req, auto cb) { c.remove(req, std::move(cb), a); })->statusCode(),
        k404NotFound);
    EXPECT_EQ(call(kBoris, Post, json{{"archived", true}}, [&](auto req, auto cb) { c.archive(req, std::move(cb), a); })
                  ->statusCode(),
              k404NotFound);
    EXPECT_EQ(tick(kBoris, a, yesterday)->statusCode(), k404NotFound) << "a check on a foreign habit";
    EXPECT_EQ(
        call(
            kBoris, Delete, json::object(), [&](auto req, auto cb) { c.deleteCheck(req, std::move(cb), a, yesterday); })
            ->statusCode(),
        k404NotFound);
    EXPECT_TRUE(list(kBoris).empty());
    EXPECT_TRUE(days(reader(kBoris), yesterday, yesterday, "UTC")["data"].empty());
    // Anna's habit and tick are untouched.
    const json mine = habit_of(days(reader(kAnna), yesterday, yesterday, "UTC")["data"], a);
    EXPECT_EQ(mine["name"], "Meditation");
    EXPECT_TRUE(day_of(mine, yesterday)["manual"]);
}

TEST_F(HabitsApiTest, TicksByHandAndTheStreak) {
    const auto a = created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    // Ticked three days back to yesterday, today open: a streak of 3, the best 3.
    for (int shift : {-3, -2, -1}) {
        ASSERT_EQ(tick(kAnna, a, utc_day(shift))->statusCode(), k200OK);
    }
    auto d = habit_of(days(reader(kAnna), utc_day(-6), utc_day(0), "UTC")["data"], a);
    EXPECT_EQ(d["streak"], 3);
    EXPECT_EQ(d["best"], 3);
    EXPECT_TRUE(d["week_done"].is_null()) << "a daily habit";
    EXPECT_FALSE(d["no_data"]);
    ASSERT_EQ(d["days"].size(), 7U);
    EXPECT_FALSE(day_of(d, utc_day(0))["done"]);
    EXPECT_TRUE(day_of(d, utc_day(-1))["manual"]);

    // Today with a value: 4 in a row, the tick's value shown.
    auto r = tick(kAnna, a, utc_day(0), json{{"value", 20}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["value"], 20);
    EXPECT_EQ(body_of(r)["data"]["date"], utc_day(0));
    d = habit_of(days(reader(kAnna), utc_day(-6), utc_day(0), "UTC")["data"], a);
    EXPECT_EQ(d["streak"], 4);
    EXPECT_EQ(day_of(d, utc_day(0))["value"], 20);

    // The tick of two days back removed: the run is yesterday and today, 2; the best still 2.
    auto del = call(
        kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteCheck(req, std::move(cb), a, utc_day(-2)); });
    EXPECT_EQ(del->statusCode(), k204NoContent);
    d = habit_of(days(reader(kAnna), utc_day(-6), utc_day(0), "UTC")["data"], a);
    EXPECT_EQ(d["streak"], 2);
    EXPECT_EQ(d["best"], 2) << "runs of 1 (three days back) and 2";

    EXPECT_EQ(tick(kAnna, a, utc_day(1))->statusCode(), k400BadRequest) << "tomorrow";
    EXPECT_EQ(tick(kAnna, a, utc_day(-367))->statusCode(), k400BadRequest) << "too far back";
    EXPECT_EQ(tick(kAnna, a, "2026-02-30")->statusCode(), k400BadRequest);
    EXPECT_EQ(tick(kAnna, a, utc_day(-1), json{{"value", -1}})->statusCode(), k400BadRequest);
}

TEST_F(HabitsApiTest, TheTicksAreReadForTheAskedDaysOnly) {
    const auto a = created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    ASSERT_EQ(tick(kAnna, a, utc_day(-1))->statusCode(), k200OK);
    // A tick from before the history year, as an old account would hold.
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("INSERT INTO habit_checks (owner_id, habit_id, date) VALUES ($1::uuid, $2::uuid, $3::date)",
                        kAnna,
                        a,
                        utc_day(-500));
        return true;
    });
    const auto week = Repositories::Habits::HabitRepository().checks(kAnna, utc_day(-6), utc_day(0));
    ASSERT_EQ(week.count(a), 1U);
    EXPECT_EQ(week.at(a).size(), 1U) << "the tick of 500 days back is not read for this week";
    EXPECT_EQ(Repositories::Habits::HabitRepository().checks(kAnna, utc_day(-600), utc_day(0)).at(a).size(), 2U);
    EXPECT_TRUE(Repositories::Habits::HabitRepository().checks(kBoris, utc_day(-600), utc_day(0)).empty());
    const auto d = habit_of(days(reader(kAnna), utc_day(-6), utc_day(0), "UTC")["data"], a);
    EXPECT_EQ(d["streak"], 1);
    EXPECT_EQ(d["best"], 1);
}

TEST_F(HabitsApiTest, AnOldRangeDoesNotCutTheStreakOffAtTheRowCap) {
    // 2100 days of steps up to yesterday. A range ~5.7 years back used to be read
    // as one window up to today: its first 2000 rows (oldest first) ended 90 days
    // ago, so the streak lost its newest days. Each window is read on its own now.
    sql(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region) VALUES ('") +
        kAnna + "', '" + kAnnaMi + "', 'x', 'x', 'sg')");
    sql(std::string("INSERT INTO daily_activity (user_id, date, steps) SELECT '") + kAnnaMi +
        "', d::date, 9000 FROM generate_series((now() AT TIME ZONE 'UTC')::date - 2100, "
        "(now() AT TIME ZONE 'UTC')::date - 1, interval '1 day') AS d");
    const auto steps =
        created(kAnna, json{{"name", "Steps 8000"}, {"schedule", "daily"}, {"source", "steps"}, {"goal", 8000}});

    const json h = habit_of(days(reader(kAnna), utc_day(-2090), utc_day(-2080), "UTC")["data"], steps);
    EXPECT_TRUE(day_of(h, utc_day(-2085))["done"]) << "the old range has its values";
    EXPECT_EQ(day_of(h, utc_day(-2085))["value"], 9000);
    EXPECT_GE(h["streak"].get<int>(), 365) << "the last year up to yesterday is whole";
}

TEST(HabitsWindows, ARangeFarFromTodayIsReadOnItsOwn) {
    using Habits::day_of;
    using Repositories::Habits::windows;
    const auto today = day_of("2026-10-14");
    const auto near = windows(day_of("2026-10-01"), day_of("2026-10-31"), today, true);
    ASSERT_EQ(near.size(), 1u) << "the range overlaps the history year";
    EXPECT_EQ(near[0].first, day_of("2025-10-14"));
    EXPECT_EQ(near[0].second, day_of("2026-10-31"));

    const auto old = windows(day_of("2020-01-01"), day_of("2020-01-31"), today, true);
    ASSERT_EQ(old.size(), 2u);
    EXPECT_EQ(old[0], std::make_pair(day_of("2020-01-01"), day_of("2020-01-31")));
    EXPECT_EQ(old[1], std::make_pair(day_of("2025-10-14"), today));

    const auto later = windows(day_of("2027-03-01"), day_of("2027-03-31"), today, true);
    ASSERT_EQ(later.size(), 2u) << "a range after today too";
    EXPECT_EQ(later[0].second, today);
    EXPECT_EQ(later[1].first, day_of("2027-03-01"));

    const auto plate = windows(day_of("2026-10-12"), today, today, false);
    ASSERT_EQ(plate.size(), 1u) << "without history only the range and today";
    EXPECT_EQ(plate[0], std::make_pair(day_of("2026-10-12"), today));

    const auto adjacent = windows(day_of("2026-10-15"), day_of("2026-10-20"), today, false);
    ASSERT_EQ(adjacent.size(), 1u) << "a range starting the day after today joins it";
    for (const auto& w : {near[0], old[0], old[1], later[0], later[1]}) {
        EXPECT_LE((w.second - w.first).count(), Repositories::Habits::kWindowsMaxDays);
    }
}

TEST_F(HabitsApiTest, TheRangeIsChecked) {
    const auto bad = [&](std::vector<std::pair<std::string, std::string>> params) {
        return call(
                   kAnna, Get, json::object(), [&](auto req, auto cb) { c.days(req, std::move(cb)); }, params)
            ->statusCode();
    };
    EXPECT_EQ(bad({{"from", "2026-10-14"}, {"to", "2026-10-13"}}), k400BadRequest) << "to before from";
    EXPECT_EQ(bad({{"from", "2026-08-01"}, {"to", "2026-10-02"}}), k400BadRequest) << "Aug 1 to Oct 2 is 63 days";
    EXPECT_EQ(bad({{"from", "2026-08-01"}, {"to", "2026-10-01"}, {"tz", "Mars/Olympus"}}), k400BadRequest) << "bad tz";
    EXPECT_EQ(bad({{"from", "2026-02-30"}, {"to", "2026-03-01"}}), k400BadRequest);
    EXPECT_EQ(bad({{"from", "2026-08-01"}, {"to", "2026-10-01"}}), k200OK) << "Aug 1 to Oct 1 is 62 days";
}

// Week Mon Sep 14 – Sun Sep 20 2026. Anna's Mi account is in region cn (UTC+8).
//   daily_activity: Sep 14 9312 steps, 512 kcal; Sep 15 7999 steps, 499 kcal.
//   sleep: ends Sep 14 23:30Z = Sep 15 07:30 local, 462 min asleep (7 h 42 m >= 7.5 h = 450);
//          ends Sep 15 22:00Z = Sep 16 06:00 local, 430 min (short).
//   Training (tz Asia/Almaty, UTC+5): finished Sep 14 10:00Z (Sep 14), Sep 16 20:30Z
//          (01:30 on Sep 17), Sep 18 12:00Z (Sep 18): 3 days in the week, 3 of 4.
TEST_F(HabitsApiTest, TheDaysCarryAutoValuesFromHealthAndTraining) {
    sql(std::string("INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region) VALUES ('") +
        kAnna + "', '" + kAnnaMi + "', 'x', 'x', 'cn')");
    sql(std::string("INSERT INTO daily_activity (user_id, date, steps, active_kcal) VALUES ('") + kAnnaMi +
        "', '2026-09-14', 9312, 512), ('" + kAnnaMi + "', '2026-09-15', 7999, 499)");
    sql(std::string("INSERT INTO sleep_sessions (user_id, sleep_id, start_at, end_at, duration_minutes, "
                    "time_asleep_minutes, time_awake_minutes) VALUES ('") +
        kAnnaMi + "', 'n1', '2026-09-14T15:00:00Z', '2026-09-14T23:30:00Z', 510, 462, 48), ('" + kAnnaMi +
        "', 'n2', '2026-09-15T14:30:00Z', '2026-09-15T22:00:00Z', 450, 430, 20)");
    for (const char* end : {"2026-09-14T10:00:00Z", "2026-09-16T20:30:00Z", "2026-09-18T12:00:00Z"}) {
        sql(std::string("INSERT INTO workout_sessions (owner_id, started_at, finished_at) VALUES ('") + kAnna + "', '" +
            end + "'::timestamptz - interval '1 hour', '" + end + "')");
    }
    const auto steps =
        created(kAnna, json{{"name", "Steps 8000"}, {"schedule", "daily"}, {"source", "steps"}, {"goal", 8000}});
    const auto kcal =
        created(kAnna, json{{"name", "Move 500"}, {"schedule", "daily"}, {"source", "active_kcal"}, {"goal", 500}});
    const auto sleep =
        created(kAnna, json{{"name", "Sleep 7.5 h"}, {"schedule", "daily"}, {"source", "sleep"}, {"goal", 7.5}});
    const auto workout =
        created(kAnna, json{{"name", "Workout"}, {"schedule", "weekly"}, {"per_week", 4}, {"source", "workout"}});

    const json out = days(reader(kAnna), "2026-09-14", "2026-09-20", "Asia/Almaty");
    EXPECT_EQ(out["from"], "2026-09-14");
    EXPECT_EQ(out["to"], "2026-09-20");

    const json s = habit_of(out["data"], steps);
    EXPECT_FALSE(s["no_data"]);
    EXPECT_TRUE(day_of(s, "2026-09-14")["done"]);
    EXPECT_FALSE(day_of(s, "2026-09-14")["manual"]);
    EXPECT_EQ(day_of(s, "2026-09-14")["value"], 9312);
    EXPECT_FALSE(day_of(s, "2026-09-15")["done"]) << "7999 < 8000";
    EXPECT_EQ(day_of(s, "2026-09-15")["value"], 7999);
    EXPECT_TRUE(day_of(s, "2026-09-16")["value"].is_null());

    const json k = habit_of(out["data"], kcal);
    EXPECT_TRUE(day_of(k, "2026-09-14")["done"]);
    EXPECT_FALSE(day_of(k, "2026-09-15")["done"]) << "499 < 500";

    const json z = habit_of(out["data"], sleep);
    EXPECT_TRUE(day_of(z, "2026-09-14")["value"].is_null()) << "the 23:30Z night belongs to Sep 15 local";
    EXPECT_TRUE(day_of(z, "2026-09-15")["done"]);
    EXPECT_EQ(day_of(z, "2026-09-15")["value"], 462);
    EXPECT_FALSE(day_of(z, "2026-09-16")["done"]) << "430 < 450";

    const json w = habit_of(out["data"], workout);
    EXPECT_FALSE(w["no_data"]);
    EXPECT_TRUE(day_of(w, "2026-09-14")["done"]);
    EXPECT_FALSE(day_of(w, "2026-09-16")["done"]) << "20:30Z is past midnight in Almaty";
    EXPECT_TRUE(day_of(w, "2026-09-17")["done"]);
    EXPECT_EQ(day_of(w, "2026-09-17")["value"], 1);
    EXPECT_TRUE(day_of(w, "2026-09-18")["done"]);
    EXPECT_EQ(w["week_done"], 3) << "3 of 4";

    // In UTC the late session stays on Sep 16.
    const json utc = habit_of(days(reader(kAnna), "2026-09-14", "2026-09-20", "UTC")["data"], workout);
    EXPECT_TRUE(day_of(utc, "2026-09-16")["done"]);
    EXPECT_FALSE(day_of(utc, "2026-09-17")["done"]);

    // Without the fitness read permission the Mi sources have no data; Training still counts.
    const json blind =
        days(user(kAnna, Domain::Permission::kGeneral), "2026-09-14", "2026-09-20", "Asia/Almaty")["data"];
    EXPECT_TRUE(habit_of(blind, steps)["no_data"]);
    EXPECT_FALSE(day_of(habit_of(blind, steps), "2026-09-14")["done"]);
    EXPECT_FALSE(habit_of(blind, workout)["no_data"]);
}

TEST_F(HabitsApiTest, ASourceWithoutAnAccountSaysNoData) {
    const auto steps =
        created(kBoris, json{{"name", "Steps 8000"}, {"schedule", "daily"}, {"source", "steps"}, {"goal", 8000}});
    const auto manual = created(kBoris, json{{"name", "Reading"}, {"schedule", "daily"}});
    const json out = days(reader(kBoris), utc_day(-2), utc_day(0), "UTC")["data"];
    EXPECT_TRUE(habit_of(out, steps)["no_data"]) << "no Mi account linked";
    EXPECT_FALSE(habit_of(out, manual)["no_data"]);
    // A tick by hand still counts on a sourced habit.
    ASSERT_EQ(tick(kBoris, steps, utc_day(-1))->statusCode(), k200OK);
    EXPECT_TRUE(
        day_of(habit_of(days(reader(kBoris), utc_day(-2), utc_day(0), "UTC")["data"], steps), utc_day(-1))["done"]);
}

// On Wed Oct 14 2026: Meditation ticked, Reading open, Swimming twice a week ticked Mon
// and Tue (met). Done 2 of 3, Reading left, Swimming 2 of 2.
TEST_F(HabitsApiTest, TheTodayDigestCarriesTheHabitsPlate) {
    const auto med = created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    created(kAnna, json{{"name", "Reading"}, {"schedule", "daily"}});
    const auto swim = created(kAnna, json{{"name", "Swimming"}, {"schedule", "weekly"}, {"per_week", 2}});
    sql(std::string("INSERT INTO habit_checks (owner_id, habit_id, date) VALUES ('") + kAnna + "', '" + med +
        "', '2026-10-14'), ('" + kAnna + "', '" + swim + "', '2026-10-12'), ('" + kAnna + "', '" + swim +
        "', '2026-10-13')");
    auto req = TestHelpers::authed(reader(kAnna), Get);
    req->setParameter("date", "2026-10-14");
    req->setParameter("tz", "UTC");
    HttpResponsePtr resp;
    today.today(req, [&](const HttpResponsePtr& r) { resp = r; });
    ASSERT_TRUE(resp);
    ASSERT_EQ(resp->statusCode(), k200OK) << std::string(resp->body());
    TestHelpers::expect_matches_schema(resp, "GET", "/api/v1/today");
    const json d = body_of(resp)["data"];
    ASSERT_TRUE(d.contains("habits")) << d.dump();
    EXPECT_EQ(d["habits"]["done"], 2);
    EXPECT_EQ(d["habits"]["total"], 3);
    EXPECT_EQ(d["habits"]["left"], json::array({"Reading"}));
    ASSERT_EQ(d["habits"]["week"].size(), 1U);
    EXPECT_EQ(d["habits"]["week"][0], (json{{"name", "Swimming"}, {"done", 2}, {"per_week", 2}}));

    // Boris has no habits: no plate.
    auto other = TestHelpers::authed(reader(kBoris), Get);
    other->setParameter("date", "2026-10-14");
    today.today(other, [&](const HttpResponsePtr& r) { resp = r; });
    EXPECT_FALSE(body_of(resp)["data"].contains("habits"));
}

// ── a daily target ──────────────────────────────────────────────────────────

TEST_F(HabitsApiTest, ATargetIsForAHabitTickedByHand) {
    auto r = call(kAnna,
                  Post,
                  json{{"name", "Water"}, {"schedule", "daily"}, {"unit", "ml"}, {"target", 2000}},
                  [&](auto req, auto cb) { c.create(req, std::move(cb)); });
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    TestHelpers::expect_matches_schema(r, "POST", "/api/v1/habits");
    EXPECT_EQ(body_of(r)["data"]["target"], 2000);
    const auto plain = created(kAnna, json{{"name", "Meditation"}, {"schedule", "daily"}});
    EXPECT_TRUE(habit_of(list(kAnna), plain)["target"].is_null()) << "no target: any tick counts";
    for (const json& bad : {
             json{{"name", "Steps"}, {"schedule", "daily"}, {"source", "steps"}, {"goal", 8000}, {"target", 10}},
             json{{"name", "Water"}, {"schedule", "daily"}, {"target", 0}},
             json{{"name", "Water"}, {"schedule", "daily"}, {"target", "2000"}},
             json{{"name", "Water"}, {"schedule", "daily"}, {"target", 1000001}},
         }) {
        EXPECT_EQ(call(kAnna, Post, bad, [&](auto req, auto cb) { c.create(req, std::move(cb)); })->statusCode(),
                  k400BadRequest)
            << bad.dump();
    }
    // An edit without the target takes it off (PATCH replaces every field).
    const auto water = body_of(r)["data"]["id"].get<std::string>();
    auto edited =
        call(kAnna, Patch, json{{"name", "Water"}, {"schedule", "daily"}, {"unit", "ml"}}, [&](auto req, auto cb) {
            c.update(req, std::move(cb), water);
        });
    ASSERT_EQ(edited->statusCode(), k200OK);
    EXPECT_TRUE(body_of(edited)["data"]["target"].is_null());
}

TEST_F(HabitsApiTest, AddingToADayReachesTheTarget) {
    auto r = call(kAnna,
                  Post,
                  json{{"name", "Water"}, {"schedule", "daily"}, {"unit", "ml"}, {"target", 2000}},
                  [&](auto req, auto cb) { c.create(req, std::move(cb)); });
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    const auto water = body_of(r)["data"]["id"].get<std::string>();
    const std::string today = utc_day(0);
    const auto add = [&](const char* who, const std::string& date, const json& body) {
        auto res = call(
            who, Post, body, [&](auto req, auto cb) { c.addCheck(req, std::move(cb), water, date); }, {{"tz", "UTC"}});
        TestHelpers::expect_2xx_matches_schema(res, "POST", "/api/v1/habits/{id}/checks/{date}/add");
        return res;
    };
    auto first = add(kAnna, today, json{{"value", 500}});
    ASSERT_EQ(first->statusCode(), k200OK) << std::string(first->body());
    EXPECT_EQ(body_of(first)["data"]["value"], 500);
    auto day = day_of(habit_of(days(reader(kAnna), today, today, "UTC")["data"], water), today);
    EXPECT_FALSE(day["done"]) << "500 of 2000";
    EXPECT_TRUE(day["manual"]);
    EXPECT_EQ(day["value"], 500);

    auto second = add(kAnna, today, json{{"value", 1500}});
    ASSERT_EQ(second->statusCode(), k200OK);
    EXPECT_EQ(body_of(second)["data"]["value"], 2000) << "500 + 1500";
    const auto h = habit_of(days(reader(kAnna), today, today, "UTC")["data"], water);
    EXPECT_TRUE(day_of(h, today)["done"]) << "2000 of 2000";
    EXPECT_EQ(h["streak"], 1);

    // A tick by PUT without a value does not count against a target; a PUT value replaces the sum.
    const std::string yesterday = utc_day(-1);
    ASSERT_EQ(tick(kAnna, water, yesterday)->statusCode(), k200OK);
    EXPECT_FALSE(day_of(habit_of(days(reader(kAnna), yesterday, yesterday, "UTC")["data"], water), yesterday)["done"]);
    ASSERT_EQ(add(kAnna, yesterday, json{{"value", 250}})->statusCode(), k200OK);
    EXPECT_EQ(day_of(habit_of(days(reader(kAnna), yesterday, yesterday, "UTC")["data"], water), yesterday)["value"],
              250)
        << "a tick without a value counts as 0";
    ASSERT_EQ(tick(kAnna, water, yesterday, json{{"value", 2100}})->statusCode(), k200OK);
    EXPECT_TRUE(day_of(habit_of(days(reader(kAnna), yesterday, yesterday, "UTC")["data"], water), yesterday)["done"]);

    for (const json& bad : {json{{"value", 0}}, json{{"value", -5}}, json{{"value", "250"}}, json::object()}) {
        EXPECT_EQ(add(kAnna, today, bad)->statusCode(), k400BadRequest) << bad.dump();
    }
    EXPECT_EQ(add(kAnna, utc_day(1), json{{"value", 250}})->statusCode(), k400BadRequest) << "tomorrow";
    EXPECT_EQ(add(kBoris, today, json{{"value", 250}})->statusCode(), k404NotFound) << "not Boris's habit";
}

// ── mood ────────────────────────────────────────────────────────────────────

TEST_F(HabitsApiTest, AMoodCheckInPerDay) {
    Database::get().execute_write([](auto& txn) {
        for (const char* id : {kAnna, kBoris}) {
            txn.exec_params("DELETE FROM mood_checkins WHERE owner_id = $1::uuid", std::string(id));
        }
        return true;
    });
    const auto put = [&](const char* who, const std::string& date, const json& body) {
        auto res =
            call(who, Put, body, [&](auto req, auto cb) { c.putMood(req, std::move(cb), date); }, {{"tz", "UTC"}});
        TestHelpers::expect_2xx_matches_schema(res, "PUT", "/api/v1/habits/mood/{date}");
        return res;
    };
    const auto moods = [&](const char* who, const std::string& from, const std::string& to) {
        auto res = call(who,
                        Get,
                        json::object(),
                        [&](auto req, auto cb) { c.listMood(req, std::move(cb)); },
                        {{"from", from}, {"to", to}});
        EXPECT_EQ(res->statusCode(), k200OK) << std::string(res->body());
        TestHelpers::expect_2xx_matches_schema(res, "GET", "/api/v1/habits/mood");
        return body_of(res)["data"];
    };
    const std::string today = utc_day(0);
    const std::string yesterday = utc_day(-1);

    auto r = put(kAnna, today, json{{"score", 4}, {"tags", json::array({"tired", "calm"})}, {"note", "Long walk"}});
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["date"], today);
    EXPECT_EQ(body_of(r)["data"]["tags"], json::array({"calm", "tired"})) << "in the order of the set";
    ASSERT_EQ(put(kAnna, yesterday, json{{"score", 2}})->statusCode(), k200OK);

    // A second check-in of the day replaces the first.
    auto again = put(kAnna, today, json{{"score", 5}});
    ASSERT_EQ(again->statusCode(), k200OK);
    EXPECT_EQ(body_of(again)["data"]["score"], 5);
    EXPECT_TRUE(body_of(again)["data"]["tags"].empty());
    EXPECT_EQ(body_of(again)["data"]["note"], "");

    auto rows = moods(kAnna, utc_day(-6), today);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0]["date"], yesterday) << "oldest first";
    EXPECT_EQ(rows[1]["score"], 5);
    EXPECT_TRUE(moods(kBoris, utc_day(-6), today).empty()) << "Anna's mood is hers";

    for (const json& bad : {
             json{{"score", 0}},
             json{{"score", 6}},
             json{{"score", 3.5}},
             json::object(),
             json{{"score", 3}, {"tags", json::array({"bored"})}},
             json{{"score", 3}, {"tags", "calm"}},
             json{{"score", 3}, {"note", std::string(501, 'x')}},
         }) {
        EXPECT_EQ(put(kAnna, today, bad)->statusCode(), k400BadRequest) << bad.dump();
    }
    EXPECT_EQ(put(kAnna, utc_day(1), json{{"score", 3}})->statusCode(), k400BadRequest) << "tomorrow";
    EXPECT_EQ(put(kAnna, "2026-02-30", json{{"score", 3}})->statusCode(), k400BadRequest);
    EXPECT_EQ(call(kAnna,
                   Get,
                   json::object(),
                   [&](auto req, auto cb) { c.listMood(req, std::move(cb)); },
                   {{"from", utc_day(-400)}, {"to", today}})
                  ->statusCode(),
              k400BadRequest)
        << "more than 366 days";

    // Boris removing "his" check-in of the day leaves Anna's.
    const auto remove = [&](const char* who, const std::string& date) {
        return call(who, Delete, json::object(), [&](auto req, auto cb) { c.deleteMood(req, std::move(cb), date); });
    };
    EXPECT_EQ(remove(kBoris, today)->statusCode(), k204NoContent);
    EXPECT_EQ(moods(kAnna, today, today).size(), 1U);
    EXPECT_EQ(remove(kAnna, today)->statusCode(), k204NoContent);
    EXPECT_TRUE(moods(kAnna, today, today).empty());
}

TEST_F(HabitsOffTest, MoodIsNotThereWhileTheModuleIsOff) {
    auto r = call(kAnna,
                  Get,
                  json::object(),
                  [&](auto req, auto cb) { c.listMood(req, std::move(cb)); },
                  {{"from", utc_day(-1)}, {"to", utc_day(0)}});
    EXPECT_EQ(r->statusCode(), k404NotFound);
}
