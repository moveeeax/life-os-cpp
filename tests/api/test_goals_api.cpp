/**
 * @file test_goals_api.cpp
 * @brief Goals module routes: each kind with its progress, one check-in per
 *        date, the binary result closing the goal, linking tasks to goals and
 *        sections of the caller only, deleting a goal unlinking its tasks,
 *        the module switch.
 */

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/GoalsController.hpp"
#include "api/TasksController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaab5";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbc5";

using TestHelpers::user;

using TestHelpers::body_of;

class GoalsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::GoalsController goals;
    Api::TasksController tasks;

    std::string config_file_name() const override { return "goals_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["goals"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE task_parse_jobs, task_notes, task_items, goal_items CASCADE");
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

    template <typename C>
    using Handler = void (C::*)(const HttpRequestPtr&, std::function<void(const HttpResponsePtr&)>&&);
    template <typename C>
    using IdHandler = void (C::*)(const HttpRequestPtr&,
                                  std::function<void(const HttpResponsePtr&)>&&,
                                  const std::string&);
    using TwoIdHandler = void (Api::GoalsController::*)(const HttpRequestPtr&,
                                                        std::function<void(const HttpResponsePtr&)>&&,
                                                        const std::string&,
                                                        const std::string&);

    // The spec path of each handler the suite drives, for the schema check
    // in call* (tests/test_helpers.hpp expect_2xx_matches_schema); a handler
    // missing here is not checked.
    static const char* spec_path_of(Handler<Api::GoalsController> h) {
        static const std::pair<Handler<Api::GoalsController>, const char*> kPaths[] = {
            {&Api::GoalsController::listGoals, "/api/v1/goals"},
            {&Api::GoalsController::createGoal, "/api/v1/goals"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }
    static const char* spec_path_of(Handler<Api::TasksController> h) {
        static const std::pair<Handler<Api::TasksController>, const char*> kPaths[] = {
            {&Api::TasksController::createItem, "/api/v1/tasks/items"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }
    static const char* spec_path_of(IdHandler<Api::GoalsController> h) {
        static const std::pair<IdHandler<Api::GoalsController>, const char*> kPaths[] = {
            {&Api::GoalsController::getGoal, "/api/v1/goals/{id}"},
            {&Api::GoalsController::updateGoal, "/api/v1/goals/{id}"},
            {&Api::GoalsController::deleteGoal, "/api/v1/goals/{id}"},
            {&Api::GoalsController::putCheckin, "/api/v1/goals/{id}/checkins"},
            {&Api::GoalsController::addSection, "/api/v1/goals/{id}/sections"},
            {&Api::GoalsController::addMilestone, "/api/v1/goals/{id}/milestones"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }
    static const char* spec_path_of(IdHandler<Api::TasksController> h) {
        static const std::pair<IdHandler<Api::TasksController>, const char*> kPaths[] = {
            {&Api::TasksController::getItem, "/api/v1/tasks/items/{id}"},
            {&Api::TasksController::updateItem, "/api/v1/tasks/items/{id}"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }
    static const char* spec_path_of(TwoIdHandler h) {
        static const std::pair<TwoIdHandler, const char*> kPaths[] = {
            {&Api::GoalsController::deleteCheckin, "/api/v1/goals/{id}/checkins/{child}"},
            {&Api::GoalsController::updateSection, "/api/v1/goals/{id}/sections/{child}"},
            {&Api::GoalsController::deleteSection, "/api/v1/goals/{id}/sections/{child}"},
            {&Api::GoalsController::deleteMilestone, "/api/v1/goals/{id}/milestones/{child}"},
        };
        for (const auto& [handler, path] : kPaths)
            if (handler == h)
                return path;
        return nullptr;
    }

    template <typename C>
    HttpResponsePtr call(C& c,
                         Handler<C> h,
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
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
        return captured;
    }

    template <typename C>
    HttpResponsePtr call_id(C& c,
                            IdHandler<C> h,
                            const std::string& who,
                            HttpMethod m,
                            const std::string& id,
                            const json& body = json::object(),
                            const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(user(who), body, m);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        (c.*h)(
            req, [&](const HttpResponsePtr& r) { captured = r; }, id);
        EXPECT_TRUE(captured);
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
        return captured;
    }

    HttpResponsePtr call_two(
        TwoIdHandler h, const std::string& who, HttpMethod m, const std::string& id, const std::string& child) {
        HttpResponsePtr captured;
        (goals.*h)(
            TestHelpers::authed_json(user(who), json::object(), m),
            [&](const HttpResponsePtr& r) { captured = r; },
            id,
            child);
        EXPECT_TRUE(captured);
        if (const char* path = spec_path_of(h))
            TestHelpers::expect_2xx_matches_schema(captured, m, path);
        return captured;
    }

    json goal(const json& body) {
        const auto resp = call(goals, &Api::GoalsController::createGoal, kAnna, Post, body, {{"date", "2026-10-08"}});
        EXPECT_EQ(resp->statusCode(), k201Created) << resp->body();
        return body_of(resp)["data"];
    }

    json task(const json& body) {
        const auto resp = call(tasks, &Api::TasksController::createItem, kAnna, Post, body);
        EXPECT_EQ(resp->statusCode(), k201Created) << resp->body();
        return body_of(resp)["data"];
    }

    json detail(const std::string& id) {
        return body_of(call_id(
            goals, &Api::GoalsController::getGoal, kAnna, Get, id, json::object(), {{"date", "2026-10-08"}}))["data"];
    }
};

class GoalsOffTest : public GoalsApiTest {
protected:
    std::string config_file_name() const override { return "goals_off_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["goals"]["enabled"] = false; }
};

}  // namespace

TEST_F(GoalsApiTest, ANumberGoalWithCheckinsOnePerDate) {
    const json g = goal({{"title", "Weight 75 kg"},
                         {"area", "health"},
                         {"kind", "number"},
                         {"start_date", "2026-09-01"},
                         {"due", "2027-03-31"},
                         {"start_value", 93},
                         {"target_value", 75},
                         {"unit", "kg"}});
    EXPECT_DOUBLE_EQ(g["progress"]["current"].get<double>(), 93) << "the start value is the first point";
    const std::string id = g["id"];
    const auto checkin = [&](const char* date, double v) {
        return call_id(goals, &Api::GoalsController::putCheckin, kAnna, Post, id, json{{"date", date}, {"value", v}});
    };
    EXPECT_EQ(checkin("2026-10-06", 91.6)->statusCode(), k201Created);
    EXPECT_EQ(checkin("2026-10-06", 91.4)->statusCode(), k201Created);
    EXPECT_EQ(checkin("2026-08-31", 94)->statusCode(), k400BadRequest) << "before the goal's start";
    const json d = detail(id);
    ASSERT_EQ(d["checkins"].size(), 1u) << "the same date replaced";
    EXPECT_DOUBLE_EQ(d["progress"]["current"].get<double>(), 91.4);
    EXPECT_EQ(d["progress"]["pace"], "behind");
    EXPECT_EQ(call_id(goals, &Api::GoalsController::putCheckin, kBoris, Post, id, json{{"value", 90}})->statusCode(),
              k404NotFound);
    EXPECT_EQ(
        call(goals,
             &Api::GoalsController::createGoal,
             kAnna,
             Post,
             json{{"title", "x"}, {"area", "health"}, {"kind", "number"}, {"due", "2027-01-01"}, {"start_value", 1}})
            ->statusCode(),
        k400BadRequest)
        << "no target_value";
    EXPECT_EQ(
        call(goals,
             &Api::GoalsController::createGoal,
             kAnna,
             Post,
             json{{"title", "x"}, {"area", "growth"}, {"kind", "steps"}, {"due", "2027-01-01"}, {"target_count", 3}})
            ->statusCode(),
        k400BadRequest)
        << "target_count is for a count goal";
    EXPECT_EQ(call(goals,
                   &Api::GoalsController::createGoal,
                   kAnna,
                   Post,
                   json{{"title", "x"}, {"area", "work"}, {"kind", "binary"}, {"due", "2027-01-01"}})
                  ->statusCode(),
              k400BadRequest)
        << "work is not an area";
}

TEST_F(GoalsApiTest, ACheckinWithoutADateLandsOnTodayInTheGivenZone) {
    // Kiritimati is UTC+14 and Pago Pago UTC-11: at any hour one of them is on another date than UTC.
    const auto [zone, local, utc] = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec(
            "SELECT z, to_char(now() AT TIME ZONE z, 'YYYY-MM-DD'), to_char(now() AT TIME ZONE 'UTC', 'YYYY-MM-DD') "
            "FROM unnest(ARRAY['Pacific/Kiritimati', 'Pacific/Pago_Pago']) AS z "
            "WHERE (now() AT TIME ZONE z)::date <> (now() AT TIME ZONE 'UTC')::date LIMIT 1");
        return std::make_tuple(
            r[0][0].template as<std::string>(), r[0][1].template as<std::string>(), r[0][2].template as<std::string>());
    });
    ASSERT_NE(local, utc);
    const json g = goal({{"title", "Weight 75 kg"},
                         {"area", "health"},
                         {"kind", "number"},
                         {"start_date", "2020-01-01"},
                         {"due", "2030-12-31"},
                         {"start_value", 93},
                         {"target_value", 75},
                         {"unit", "kg"}});
    const auto there =
        call_id(goals, &Api::GoalsController::putCheckin, kAnna, Post, g["id"], json{{"value", 90}}, {{"tz", zone}});
    ASSERT_EQ(there->statusCode(), k201Created) << there->body();
    EXPECT_EQ(body_of(there)["data"]["date"], local);
    const auto plain = call_id(goals, &Api::GoalsController::putCheckin, kAnna, Post, g["id"], json{{"value", 89}});
    ASSERT_EQ(plain->statusCode(), k201Created) << plain->body();
    EXPECT_EQ(body_of(plain)["data"]["date"], utc) << "no tz: UTC";
    EXPECT_EQ(detail(g["id"])["checkins"].size(), 2u) << "two days, two check-ins";
}

TEST_F(GoalsApiTest, ACheckinAfterTheCallersTodayIsRefused) {
    // Pago Pago is UTC-11: its tomorrow is never past today in Kiritimati (UTC+14), the bound without a tz.
    const auto [tomorrow, far] = Database::get().execute_read([](auto& txn) {
        auto r = txn.exec(
            "SELECT to_char((now() AT TIME ZONE 'Pacific/Pago_Pago')::date + 1, 'YYYY-MM-DD'), "
            "to_char((now() AT TIME ZONE 'UTC')::date + 2, 'YYYY-MM-DD')");
        return std::make_pair(r[0][0].template as<std::string>(), r[0][1].template as<std::string>());
    });
    const json g = goal({{"title", "Weight 75 kg"},
                         {"area", "health"},
                         {"kind", "number"},
                         {"start_date", "2020-01-01"},
                         {"due", "2030-12-31"},
                         {"start_value", 93},
                         {"target_value", 75},
                         {"unit", "kg"}});
    const auto put = [&](const std::string& date, const std::vector<std::pair<std::string, std::string>>& params) {
        return call_id(goals,
                       &Api::GoalsController::putCheckin,
                       kAnna,
                       Post,
                       g["id"],
                       json{{"date", date}, {"value", 90}},
                       params);
    };
    const auto there = put(tomorrow, {{"tz", "Pacific/Pago_Pago"}});
    EXPECT_EQ(there->statusCode(), k400BadRequest) << "tomorrow in the caller's zone";
    EXPECT_EQ(body_of(there)["error"], "future_date") << there->body();
    EXPECT_EQ(put(far, {})->statusCode(), k400BadRequest) << "two days past UTC is past every zone";
    EXPECT_EQ(put(tomorrow, {})->statusCode(), k201Created) << "without a tz the bound is UTC+14";
    EXPECT_EQ(detail(g["id"])["checkins"].size(), 1u);
}

TEST_F(GoalsApiTest, StepsAndCountByTheirTasks) {
    const json course = goal({{"title", "SA Pro course"},
                              {"area", "growth"},
                              {"kind", "steps"},
                              {"start_date", "2026-09-15"},
                              {"due", "2026-12-15"}});
    const std::string cid = course["id"];
    const json section = body_of(
        call_id(goals, &Api::GoalsController::addSection, kAnna, Post, cid, json{{"name", "Migration"}}))["data"];
    task({{"title", "Lectures 41-52"},
          {"area", "growth"},
          {"goal_id", cid},
          {"goal_section_id", section["id"]},
          {"status", "done"}});
    task({{"title", "Lab: DMS"},
          {"area", "growth"},
          {"goal_id", cid},
          {"goal_section_id", section["id"]},
          {"status", "in_progress"}});
    task({{"title", "Practice exam"}, {"area", "growth"}, {"goal_id", cid}});
    const json d = detail(cid);
    EXPECT_EQ(d["progress"]["done"], 1);
    EXPECT_EQ(d["progress"]["total"], 3);
    ASSERT_EQ(d["sections"].size(), 1u);
    EXPECT_EQ(d["sections"][0]["tasks"].size(), 2u);
    EXPECT_EQ(d["tasks"].size(), 1u) << "a task without a section apart";

    const json books = goal({{"title", "12 books"},
                             {"area", "growth"},
                             {"kind", "count"},
                             {"start_date", "2026-01-01"},
                             {"due", "2026-12-31"},
                             {"target_count", 12}});
    task({{"title", "Deep Work"}, {"area", "growth"}, {"goal_id", books["id"]}, {"status", "done"}});
    EXPECT_EQ(detail(books["id"])["progress"]["done"], 1);
    EXPECT_EQ(
        call_id(goals, &Api::GoalsController::addSection, kAnna, Post, books["id"], json{{"name", "x"}})->statusCode(),
        k400BadRequest)
        << "sections are for a steps goal";
}

TEST_F(GoalsApiTest, ATaskLinksOnlyToTheCallersGoalAndItsSections) {
    const json mine = goal({{"title", "Mine"}, {"area", "growth"}, {"kind", "steps"}, {"due", "2027-01-01"}});
    const json other = goal({{"title", "Other"}, {"area", "growth"}, {"kind", "steps"}, {"due", "2027-01-01"}});
    const json other_section = body_of(
        call_id(goals, &Api::GoalsController::addSection, kAnna, Post, other["id"], json{{"name", "S"}}))["data"];
    const auto post = [&](const std::string& who, const json& b) {
        return call(tasks, &Api::TasksController::createItem, who, Post, b)->statusCode();
    };
    EXPECT_EQ(post(kBoris, json{{"title", "x"}, {"area", "growth"}, {"goal_id", mine["id"]}}), k400BadRequest)
        << "another user's goal";
    EXPECT_EQ(
        post(
            kAnna,
            json{
                {"title", "x"}, {"area", "growth"}, {"goal_id", mine["id"]}, {"goal_section_id", other_section["id"]}}),
        k400BadRequest)
        << "a section of a different goal";
    EXPECT_EQ(post(kAnna, json{{"title", "x"}, {"area", "growth"}, {"goal_section_id", other_section["id"]}}),
              k400BadRequest)
        << "a section without its goal";
    const json mine_section = body_of(
        call_id(goals, &Api::GoalsController::addSection, kAnna, Post, mine["id"], json{{"name", "M"}}))["data"];
    const json t =
        task({{"title", "x"}, {"area", "growth"}, {"goal_id", mine["id"]}, {"goal_section_id", mine_section["id"]}});
    EXPECT_EQ(t["goal_title"], "Mine");
    // Moving the task to another goal drops the old goal's section.
    const auto moved =
        call_id(tasks, &Api::TasksController::updateItem, kAnna, Patch, t["id"], json{{"goal_id", other["id"]}});
    ASSERT_EQ(moved->statusCode(), k200OK) << moved->body();
    EXPECT_EQ(body_of(moved)["data"]["goal_id"], other["id"]);
    EXPECT_TRUE(body_of(moved)["data"]["goal_section_id"].is_null()) << "a section of the old goal does not travel";
}

TEST_F(GoalsApiTest, ABinaryResultClosesTheGoalAndStays) {
    const json exam = goal({{"title", "Pass SA Pro"},
                            {"area", "growth"},
                            {"kind", "binary"},
                            {"start_date", "2026-09-15"},
                            {"due", "2026-10-18"}});
    EXPECT_EQ(exam["progress"]["pace"], "behind") << "10 days left";
    EXPECT_EQ(body_of(call_id(goals,
                              &Api::GoalsController::addMilestone,
                              kAnna,
                              Post,
                              exam["id"],
                              json{{"date", "2026-10-15"}, {"label", "Exam booked"}}))["data"]["label"],
              "Exam booked");
    const auto patch = [&](const json& b) {
        return call_id(goals, &Api::GoalsController::updateGoal, kAnna, Patch, exam["id"], b, {{"date", "2026-10-08"}});
    };
    const auto passed = patch(json{{"result", "pass"}});
    ASSERT_EQ(passed->statusCode(), k200OK) << passed->body();
    EXPECT_EQ(body_of(passed)["data"]["status"], "done");
    EXPECT_TRUE(body_of(passed)["data"]["completed_at"].is_string());
    EXPECT_EQ(body_of(passed)["data"]["progress"]["pace"], "passed");
    EXPECT_EQ(patch(json{{"result", nullptr}})->statusCode(), k400BadRequest) << "a result is not taken back";
    EXPECT_EQ(patch(json{{"kind", "number"}})->statusCode(), k400BadRequest) << "the kind does not change";
    EXPECT_EQ(patch(json{{"due", nullptr}})->statusCode(), k400BadRequest) << "a goal keeps a due date (not a 500)";
    EXPECT_EQ(patch(json{{"status", "active"}})->statusCode(), k400BadRequest) << "a result keeps the goal closed";
    const json open_exam =
        goal({{"title", "Another exam"}, {"area", "growth"}, {"kind", "binary"}, {"due", "2027-01-01"}});
    EXPECT_EQ(call_id(goals, &Api::GoalsController::updateGoal, kAnna, Patch, open_exam["id"], json{{"status", "done"}})
                  ->statusCode(),
              k400BadRequest)
        << "a pass / fail goal is done by its result";
}

TEST_F(GoalsApiTest, DeletingAGoalLeavesItsTasks) {
    const json g = goal({{"title", "G"}, {"area", "projects"}, {"kind", "steps"}, {"due", "2027-01-01"}});
    const json t = task({{"title", "Step"}, {"area", "projects"}, {"goal_id", g["id"]}});
    EXPECT_EQ(call_id(goals, &Api::GoalsController::deleteGoal, kAnna, Delete, g["id"])->statusCode(), k204NoContent);
    const json after = body_of(call_id(tasks, &Api::TasksController::getItem, kAnna, Get, t["id"]))["data"];
    EXPECT_TRUE(after["goal_id"].is_null());
    EXPECT_EQ(call_two(&Api::GoalsController::deleteCheckin, kAnna, Delete, g["id"], g["id"])->statusCode(),
              k404NotFound);
}

TEST_F(GoalsOffTest, EveryRouteIs404WhileTheModuleIsOff) {
    EXPECT_EQ(call(goals, &Api::GoalsController::listGoals, kAnna, Get)->statusCode(), k404NotFound);
}
