/**
 * @file test_task_projects_api.cpp
 * @brief Projects and courses under /api/v1/tasks/projects: statuses by kind, a
 *        code made from the name and kept unique, the open and done counts and
 *        the next task, a task linked to the caller's project only (also through
 *        a parse accept line), the ?project= filter, a deleted project leaving
 *        its tasks unlinked.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/ProjectsController.hpp"
#include "api/TasksController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaad21";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbd22";

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;

class TaskProjectsApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::ProjectsController c;
    Api::TasksController tasks;

    std::string config_file_name() const override { return "task_projects_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override { cfg["tasks"]["enabled"] = true; }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM task_items WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM projects WHERE owner_id = $1::uuid", o);
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) "
                    "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) "
                    "ON CONFLICT DO NOTHING",
                    o,
                    o + "@example.test");
            }
            return true;
        });
    }

    template <typename Fn>
    HttpResponsePtr call(const char* who,
                         HttpMethod method,
                         const json& body,
                         Fn fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(principal(who), body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr create(const char* who, const json& body) {
        auto r = call(who, Post, body, [&](auto req, auto cb) { c.createProject(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/tasks/projects");
        return r;
    }

    std::string project(const char* who, const std::string& name) {
        auto r = create(who, json{{"name", name}});
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"]["id"].get<std::string>();
    }

    HttpResponsePtr add_task(const char* who, const json& extra) {
        json body = {{"title", "Write the runbook"}, {"area", "projects"}};
        body.update(extra);
        auto r = call(who, Post, body, [&](auto req, auto cb) { tasks.createItem(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/tasks/items");
        return r;
    }
};

}  // namespace

TEST_F(TaskProjectsApiTest, StatusesFollowTheKind) {
    auto p = create(kAnna, json{{"name", "Small.kz — Platform Engineering"}});
    ASSERT_EQ(p->statusCode(), k201Created) << std::string(p->body());
    EXPECT_EQ(body_of(p)["data"]["status"], "active") << "a project starts active";
    EXPECT_EQ(body_of(p)["data"]["kind"], "project");
    auto course = create(kAnna,
                         json{{"name", "AWS SA Pro"},
                              {"kind", "course"},
                              {"platform", "A Cloud Guru"},
                              {"starts_on", "2026-10-01"},
                              {"ends_on", "2027-03-01"}});
    ASSERT_EQ(course->statusCode(), k201Created) << std::string(course->body());
    EXPECT_EQ(body_of(course)["data"]["status"], "planned") << "a course starts planned";
    EXPECT_EQ(create(kAnna, json{{"name", "X"}, {"kind", "course"}, {"status", "paused"}})->statusCode(),
              k400BadRequest)
        << "a course is never paused";
    EXPECT_EQ(create(kAnna, json{{"name", "X"}, {"platform", "Coursera"}})->statusCode(), k400BadRequest)
        << "only a course has a platform";
    EXPECT_EQ(create(kAnna, json{{"name", "X"}, {"link", "javascript:alert(1)"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(
        create(kAnna, json{{"name", "X"}, {"kind", "course"}, {"starts_on", "2026-10-01"}, {"ends_on", "2026-09-01"}})
            ->statusCode(),
        k400BadRequest)
        << "ends before it starts";
    const auto id = body_of(course)["data"]["id"].get<std::string>();
    EXPECT_EQ(call(kAnna,
                   Patch,
                   json{{"kind", "project"}},
                   [&](auto req, auto cb) { c.updateProject(req, std::move(cb), id); })
                  ->statusCode(),
              k400BadRequest)
        << "the kind does not change";
}

TEST_F(TaskProjectsApiTest, AWrongKindTypeIs400) {
    // A null or numeric kind used to throw out of body.value() as 500.
    EXPECT_EQ(create(kAnna, json{{"name", "X"}, {"kind", 5}})->statusCode(), k400BadRequest);
    auto nulled = create(kAnna, json{{"name", "Y"}, {"kind", nullptr}});
    ASSERT_EQ(nulled->statusCode(), k201Created) << std::string(nulled->body());
    EXPECT_EQ(body_of(nulled)["data"]["kind"], "project") << "a null kind is the default";
}

TEST_F(TaskProjectsApiTest, ACodeIsMadeFromTheNameAndKeptUnique) {
    auto a = create(kAnna, json{{"name", "Small.kz — Platform Engineering"}});
    // "small" "-" "kz" "-" "platform" "-" "engine": cut at 24 characters.
    EXPECT_EQ(body_of(a)["data"]["code"], "small-kz-platform-engine");
    auto b = create(kAnna, json{{"name", "Small.kz — Platform Engineering"}});
    EXPECT_EQ(body_of(b)["data"]["code"], "small-kz-platform-engi-2") << "the suffix keeps it within 24";
    EXPECT_EQ(create(kAnna, json{{"name", "Other"}, {"code", "small-kz-platform-engine"}})->statusCode(), k409Conflict);
    EXPECT_EQ(create(kBoris, json{{"name", "Other"}, {"code", "small-kz-platform-engine"}})->statusCode(), k201Created)
        << "codes are unique per owner";
    EXPECT_EQ(create(kAnna, json{{"name", "Other"}, {"code", "Bad Code"}})->statusCode(), k400BadRequest);
}

TEST_F(TaskProjectsApiTest, CountsAndTheNextTask) {
    const auto p = project(kAnna, "Life OS");
    add_task(kAnna, json{{"project_id", p}, {"title", "Someday"}});
    add_task(kAnna, json{{"project_id", p}, {"title", "Due first"}, {"due", "2026-10-20"}});
    add_task(kAnna, json{{"project_id", p}, {"title", "Shipped"}, {"status", "done"}});
    add_task(kAnna,
             json{{"project_id", p}, {"title", "Working on it"}, {"status", "in_progress"}, {"due", "2026-10-18"}});
    add_task(kAnna, json{{"title", "No project"}});
    auto list = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.listProjects(req, std::move(cb)); });
    TestHelpers::expect_2xx_matches_schema(list, "GET", "/api/v1/tasks/projects");
    ASSERT_EQ(body_of(list)["data"].size(), 1U);
    const auto row = body_of(list)["data"][0];
    EXPECT_EQ(row["open"], 3) << "in progress counts as open";
    EXPECT_EQ(row["done"], 1);
    EXPECT_EQ(row["next"]["title"], "Working on it") << "the earliest due first, in progress included";
    auto filtered = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { tasks.listItems(req, std::move(cb)); }, {{"project", p}});
    TestHelpers::expect_2xx_matches_schema(filtered, "GET", "/api/v1/tasks/items");
    EXPECT_EQ(body_of(filtered)["data"].size(), 4U);
    EXPECT_EQ(body_of(filtered)["data"][0]["project_title"], "Life OS");
}

TEST_F(TaskProjectsApiTest, ATaskTakesOnlyTheCallersProject) {
    const auto theirs = project(kBoris, "Boris's");
    EXPECT_EQ(add_task(kAnna, json{{"project_id", theirs}})->statusCode(), k400BadRequest);
    const auto mine = project(kAnna, "Mine");
    auto t = add_task(kAnna, json{{"project_id", mine}});
    ASSERT_EQ(t->statusCode(), k201Created) << std::string(t->body());
    const auto task = body_of(t)["data"]["id"].get<std::string>();
    EXPECT_EQ(call(kAnna,
                   Patch,
                   json{{"project_id", theirs}},
                   [&](auto req, auto cb) { tasks.updateItem(req, std::move(cb), task); })
                  ->statusCode(),
              k400BadRequest);
    auto cleared = call(kAnna, Patch, json{{"project_id", nullptr}}, [&](auto req, auto cb) {
        tasks.updateItem(req, std::move(cb), task);
    });
    ASSERT_EQ(cleared->statusCode(), k200OK);
    TestHelpers::expect_matches_schema(cleared, "PATCH", "/api/v1/tasks/items/{id}");
    EXPECT_TRUE(body_of(cleared)["data"]["project_id"].is_null());
}

TEST_F(TaskProjectsApiTest, ADeletedProjectLeavesItsTasks) {
    const auto p = project(kAnna, "Short-lived");
    const auto task = body_of(add_task(kAnna, json{{"project_id", p}}))["data"]["id"].get<std::string>();
    EXPECT_EQ(call(kBoris, Delete, json::object(), [&](auto req, auto cb) { c.deleteProject(req, std::move(cb), p); })
                  ->statusCode(),
              k404NotFound);
    EXPECT_EQ(call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteProject(req, std::move(cb), p); })
                  ->statusCode(),
              k204NoContent);
    auto left = call(kAnna, Get, json::object(), [&](auto req, auto cb) { tasks.getItem(req, std::move(cb), task); });
    ASSERT_EQ(left->statusCode(), k200OK);
    TestHelpers::expect_matches_schema(left, "GET", "/api/v1/tasks/items/{id}");
    EXPECT_TRUE(body_of(left)["data"]["project_id"].is_null());
}
