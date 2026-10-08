/**
 * @file test_goals_api.cpp
 * @brief Goals module routes: each kind with its progress, one check-in per
 *        date, the binary result closing the goal, linking tasks to goals and
 *        sections of the caller only, deleting a goal unlinking its tasks,
 *        the module switch.
 */

#include <string>
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

Security::Auth::AuthPrincipal user(const std::string& id) {
    Security::Auth::AuthPrincipal p;
    p.subject = id;
    p.raw_claims = json{{"sub", id}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

json body_of(const HttpResponsePtr& resp) {
    return json::parse(std::string(resp->body()));
}

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
    const json t = task({{"title", "x"}, {"area", "growth"}, {"goal_id", mine["id"]}});
    EXPECT_EQ(t["goal_title"], "Mine");
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
