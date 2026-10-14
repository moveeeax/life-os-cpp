/**
 * @file test_notes_api.cpp
 * @brief Notes under /api/v1/notes: the fields of a tarassov.me post, a slug
 *        made from the title and kept unique, tags checked, search and the
 *        topic, tag and project filters with the facets, a project or task link
 *        of the caller only, private for now, owner scope, a deleted project
 *        leaving its notes unlinked.
 */

#include <thread>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/NotesController.hpp"
#include "api/ProjectsController.hpp"
#include "api/TasksController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaae31";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbe32";

Security::Auth::AuthPrincipal principal(const char* who) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;

class NotesApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::NotesController c;
    Api::ProjectsController projects;
    Api::TasksController tasks;

    std::string config_file_name() const override { return "notes_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["notes"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM notes WHERE owner_id = $1::uuid", o);
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

    HttpResponsePtr add(const char* who, const json& body) {
        return call(who, Post, body, [&](auto req, auto cb) { c.createNote(req, std::move(cb)); });
    }

    json list(const char* who, const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto r = call(
            who, Get, json::object(), [&](auto req, auto cb) { c.listNotes(req, std::move(cb)); }, params);
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        return body_of(r);
    }

    std::string project(const char* who) {
        auto r = call(who, Post, json{{"name", "Small.kz"}}, [&](auto req, auto cb) {
            projects.createProject(req, std::move(cb));
        });
        return body_of(r)["data"]["id"].get<std::string>();
    }
};

}  // namespace

TEST_F(NotesApiTest, TheFieldsOfAPost) {
    auto r = add(kAnna,
                 json{{"title", "VPC design: transit gateway vs peering"},
                      {"summary", "When a hub beats one-to-one links"},
                      {"body", "# VPC design\n\nPeering is **one to one**."},
                      {"topic", "AWS"},
                      {"tags", {"vpc", " networking ", "vpc"}}});
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    const auto n = body_of(r)["data"];
    EXPECT_EQ(n["slug"], "vpc-design-transit-gateway-vs-peering");
    EXPECT_EQ(n["status"], "private");
    EXPECT_EQ(n["tags"], json({"vpc", "networking"})) << "trimmed, no repeats";
    EXPECT_EQ(n["body"], "# VPC design\n\nPeering is **one to one**.");
    EXPECT_TRUE(n["published_at"].is_null());
    for (const char* key :
         {"id", "slug", "title", "summary", "body", "status", "topic", "tags", "created_at", "updated_at"}) {
        EXPECT_TRUE(n.contains(key)) << key << " as on a tarassov.me post";
    }
}

TEST_F(NotesApiTest, ASlugIsUniquePerOwner) {
    EXPECT_EQ(body_of(add(kAnna, json{{"title", "My note"}}))["data"]["slug"], "my-note");
    EXPECT_EQ(body_of(add(kAnna, json{{"title", "My note"}}))["data"]["slug"], "my-note-2");
    EXPECT_EQ(body_of(add(kBoris, json{{"title", "My note"}}))["data"]["slug"], "my-note") << "per owner";
    EXPECT_EQ(add(kAnna, json{{"title", "Other"}, {"slug", "my-note"}})->statusCode(), k409Conflict);
    EXPECT_EQ(add(kAnna, json{{"title", "Other"}, {"slug", "Bad Slug"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(add(kAnna, json{{"title", "Стиль разборов"}}))["data"]["slug"], "stil-razborov");
}

TEST_F(NotesApiTest, BadFieldsAreRefused) {
    EXPECT_EQ(add(kAnna, json{{"title", ""}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"tags", {"a,b"}}})->statusCode(), k400BadRequest) << "a comma";
    json many = json::array();
    for (int i = 0; i < 21; ++i) {
        many.push_back("t" + std::to_string(i));
    }
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"tags", many}})->statusCode(), k400BadRequest) << "21 tags";
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"status", "published"}})->statusCode(), k400BadRequest) << "not yet";
    std::string cyrillic;
    for (int i = 0; i < 40; ++i) {
        cyrillic += "ж";
    }
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"tags", {cyrillic}}})->statusCode(), k201Created)
        << "40 letters, 80 bytes";
    EXPECT_EQ(add(kAnna, json{{"title", "y"}, {"tags", {cyrillic + "ж"}}})->statusCode(), k400BadRequest);
}

TEST_F(NotesApiTest, SearchFiltersAndFacets) {
    const auto p = project(kAnna);
    add(kAnna,
        json{{"title", "OTel Collector routing"}, {"topic", "Kubernetes"}, {"tags", {"otel"}}, {"project_id", p}});
    add(kAnna, json{{"title", "VPC design"}, {"topic", "AWS"}, {"tags", {"vpc", "otel"}}, {"body", "transit gateway"}});
    add(kAnna, json{{"title", "100% sure"}, {"topic", "Life"}});
    EXPECT_EQ(list(kAnna, {{"q", "gateway"}})["data"].size(), 1U) << "the body is searched";
    EXPECT_EQ(list(kAnna, {{"q", "%"}})["data"].size(), 1U) << "% is literal";
    EXPECT_EQ(list(kAnna, {{"topic", "AWS"}})["data"].size(), 1U);
    EXPECT_EQ(list(kAnna, {{"tag", "otel"}})["data"].size(), 2U);
    const auto by_project = list(kAnna, {{"project", p}});
    ASSERT_EQ(by_project["data"].size(), 1U);
    EXPECT_EQ(by_project["data"][0]["project_title"], "Small.kz");
    EXPECT_FALSE(by_project["data"][0].contains("body")) << "the list leaves the body out";
    const auto all = list(kAnna);
    EXPECT_EQ(all["total"], 3);
    EXPECT_EQ(all["tags"][0], json({{"tag", "otel"}, {"count", 2}}));
    EXPECT_EQ(all["topics"].size(), 3U);
    EXPECT_EQ(list(kBoris)["total"], 0);
}

TEST_F(NotesApiTest, LinksAreTheCallersOnly) {
    const auto theirs = project(kBoris);
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"project_id", theirs}})->statusCode(), k400BadRequest);
    auto task = call(kBoris, Post, json{{"title", "Their task"}, {"area", "projects"}}, [&](auto req, auto cb) {
        tasks.createItem(req, std::move(cb));
    });
    const auto their_task = body_of(task)["data"]["id"].get<std::string>();
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"task_id", their_task}})->statusCode(), k400BadRequest);
    const auto id = body_of(add(kAnna, json{{"title", "Mine"}}))["data"]["id"].get<std::string>();
    EXPECT_EQ(
        call(kBoris, Get, json::object(), [&](auto req, auto cb) { c.getNote(req, std::move(cb), id); })->statusCode(),
        k404NotFound);
    EXPECT_EQ(
        call(kBoris, Patch, json{{"title", "x"}}, [&](auto req, auto cb) { c.updateNote(req, std::move(cb), id); })
            ->statusCode(),
        k404NotFound);
}

TEST_F(NotesApiTest, EditAndADeletedProject) {
    const auto p = project(kAnna);
    const auto id = body_of(add(kAnna, json{{"title", "Draft"}, {"project_id", p}}))["data"]["id"].get<std::string>();
    auto edited = call(kAnna, Patch, json{{"body", "More words"}, {"tags", {"k8s"}}}, [&](auto req, auto cb) {
        c.updateNote(req, std::move(cb), id);
    });
    ASSERT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    EXPECT_EQ(body_of(edited)["data"]["body"], "More words");
    EXPECT_EQ(body_of(edited)["data"]["title"], "Draft") << "untouched fields stay";
    call(kAnna, Delete, json::object(), [&](auto req, auto cb) { projects.deleteProject(req, std::move(cb), p); });
    auto left = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getNote(req, std::move(cb), id); });
    EXPECT_TRUE(body_of(left)["data"]["project_id"].is_null());
    EXPECT_EQ(call(kAnna, Delete, json::object(), [&](auto req, auto cb) { c.deleteNote(req, std::move(cb), id); })
                  ->statusCode(),
              k204NoContent);
}

namespace {

/// updated_at of one row at microsecond precision; the text orders as time does.
std::string updated_at_of(const std::string& sql, const std::string& param) {
    return Database::get().execute_read(
        [&](auto& txn) { return txn.exec_params(sql, param)[0][0].template as<std::string>(); });
}

constexpr const char* kUpdatedAtFormat = "to_char(updated_at, 'YYYY-MM-DD HH24:MI:SS.US')";

}  // namespace

// The trigger of migration 042 bumps updated_at; NoteRepository::update no
// longer sets the column by hand.
TEST_F(NotesApiTest, UpdatedAtMovesByTheTriggerAlone) {
    const auto id = body_of(add(kAnna, json{{"title", "Draft"}}))["data"]["id"].get<std::string>();
    const std::string sql = std::string("SELECT ") + kUpdatedAtFormat + " FROM notes WHERE id = $1::uuid";
    const std::string before = updated_at_of(sql, id);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto edited = call(kAnna, Patch, json{{"body", "More words"}}, [&](auto req, auto cb) {
        c.updateNote(req, std::move(cb), id);
    });
    ASSERT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    EXPECT_GT(updated_at_of(sql, id), before) << "notes_touch_updated_at";
}
