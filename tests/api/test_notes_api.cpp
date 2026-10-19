/**
 * @file test_notes_api.cpp
 * @brief Notes under /api/v1/notes: the fields of a tarassov.me post, a slug
 *        made from the title and kept unique, tags checked, search and the
 *        topic, tag and project filters with the facets, a project or task link
 *        of the caller only, private for now, owner scope, a deleted project
 *        leaving its notes unlinked, the day a note is about ordering the
 *        list and bounding it with from and to, and a deal of the practice
 *        linked, filtered on and unlinked when the deal goes.
 */

#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/NotesController.hpp"
#include "api/PracticeController.hpp"
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
    Api::PracticeController practice;

    std::string config_file_name() const override { return "notes_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["notes"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
        cfg["practice"]["enabled"] = true;
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
                txn.exec_params("DELETE FROM practice_entries WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM practice_deals WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM practice_clients WHERE owner_id = $1::uuid", o);
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
        auto r = call(who, Post, body, [&](auto req, auto cb) { c.createNote(req, std::move(cb)); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/notes");
        return r;
    }

    json list(const char* who, const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto r = call(
            who, Get, json::object(), [&](auto req, auto cb) { c.listNotes(req, std::move(cb)); }, params);
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/notes");
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
    TestHelpers::expect_matches_schema(edited, "PATCH", "/api/v1/notes/{id}");
    EXPECT_EQ(body_of(edited)["data"]["body"], "More words");
    EXPECT_EQ(body_of(edited)["data"]["title"], "Draft") << "untouched fields stay";
    call(kAnna, Delete, json::object(), [&](auto req, auto cb) { projects.deleteProject(req, std::move(cb), p); });
    auto left = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getNote(req, std::move(cb), id); });
    TestHelpers::expect_2xx_matches_schema(left, "GET", "/api/v1/notes/{id}");
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
    auto edited = call(
        kAnna, Patch, json{{"body", "More words"}}, [&](auto req, auto cb) { c.updateNote(req, std::move(cb), id); });
    ASSERT_EQ(edited->statusCode(), k200OK) << std::string(edited->body());
    EXPECT_GT(updated_at_of(sql, id), before) << "notes_touch_updated_at";
}

// A note about a day sorts by that day; one without a date by the day it was made (today here).
TEST_F(NotesApiTest, ADateOrdersTheList) {
    auto r = add(kAnna, json{{"title", "Trip to Almaty"}, {"date", "2026-05-02"}});
    ASSERT_EQ(r->statusCode(), k201Created) << std::string(r->body());
    EXPECT_EQ(body_of(r)["data"]["date"], "2026-05-02");
    const auto may = body_of(r)["data"]["id"].get<std::string>();
    const auto undated = body_of(add(kAnna, json{{"title", "Made today"}}))["data"];
    EXPECT_TRUE(undated["date"].is_null());
    const auto future = body_of(add(kAnna, json{{"title", "Plan for 2099"}, {"date", "2099-01-01"}}))["data"];
    const auto march = body_of(add(kAnna, json{{"title", "Spring"}, {"date", "2026-03-10"}}))["data"];

    auto rows = list(kAnna)["data"];
    ASSERT_EQ(rows.size(), 4U);
    EXPECT_EQ(rows[0]["id"], future["id"]) << "the latest day first";
    EXPECT_EQ(rows[1]["id"], undated["id"]) << "an undated note sits at the day it was made";
    EXPECT_EQ(rows[2]["id"], may);
    EXPECT_EQ(rows[3]["id"], march["id"]);
    EXPECT_EQ(rows[2]["date"], "2026-05-02") << "the list carries the date";

    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"date", "2026-02-30"}})->statusCode(), k400BadRequest);
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"date", "May 2"}})->statusCode(), k400BadRequest);

    // An edit moves the date or clears it with null; leaving it out keeps it.
    const auto patch = [&](const json& body) {
        return call(kAnna, Patch, body, [&](auto req, auto cb) { c.updateNote(req, std::move(cb), may); });
    };
    auto moved = patch(json{{"date", "2026-01-15"}});
    ASSERT_EQ(moved->statusCode(), k200OK) << std::string(moved->body());
    TestHelpers::expect_matches_schema(moved, "PATCH", "/api/v1/notes/{id}");
    EXPECT_EQ(body_of(moved)["data"]["date"], "2026-01-15");
    EXPECT_EQ(body_of(patch(json{{"title", "Trip"}}))["data"]["date"], "2026-01-15");
    EXPECT_EQ(list(kAnna)["data"][3]["id"], may) << "now the oldest";
    EXPECT_TRUE(body_of(patch(json{{"date", nullptr}}))["data"]["date"].is_null());
}

// from and to bound the day a note is about, both inclusive; an undated note is out once a bound is given.
TEST_F(NotesApiTest, FromAndToFilterOnTheDate) {
    const auto id_of = [&](const json& body) {
        auto r = add(kAnna, body);
        EXPECT_EQ(r->statusCode(), k201Created) << std::string(r->body());
        return body_of(r)["data"]["id"].get<std::string>();
    };
    const auto march = id_of(json{{"title", "Spring"}, {"date", "2026-03-10"}});
    const auto may = id_of(json{{"title", "Almaty"}, {"date", "2026-05-02"}});
    const auto june = id_of(json{{"title", "Summer"}, {"date", "2026-06-30"}});
    id_of(json{{"title", "Undated"}});
    add(kBoris, json{{"title", "Theirs"}, {"date", "2026-05-02"}});

    const auto ids = [&](const std::vector<std::pair<std::string, std::string>>& params) {
        const json page = list(kAnna, params);
        const json rows = page["data"];
        std::vector<std::string> out;
        for (const auto& row : rows) {
            out.push_back(row["id"].get<std::string>());
        }
        EXPECT_EQ(page["total"], static_cast<long>(out.size())) << "the total counts the same notes";
        return out;
    };
    EXPECT_EQ(ids({{"from", "2026-03-10"}, {"to", "2026-05-02"}}), (std::vector<std::string>{may, march}))
        << "both bounds inclusive, newest first";
    EXPECT_EQ(ids({{"from", "2026-05-03"}}), (std::vector<std::string>{june}));
    EXPECT_EQ(ids({{"to", "2026-03-10"}}), (std::vector<std::string>{march}));
    EXPECT_EQ(ids({{"from", "2026-05-02"}, {"to", "2026-05-02"}}), (std::vector<std::string>{may})) << "one day";
    EXPECT_EQ(ids({}).size(), 4U) << "no bound: the undated note is there";

    for (const std::vector<std::pair<std::string, std::string>>& bad : {
             std::vector<std::pair<std::string, std::string>>{{"from", "2026-02-30"}},
             std::vector<std::pair<std::string, std::string>>{{"to", "May 2"}},
             std::vector<std::pair<std::string, std::string>>{{"from", "2026-06-01"}, {"to", "2026-05-01"}},
         }) {
        auto r = call(
            kAnna, Get, json::object(), [&](auto req, auto cb) { c.listNotes(req, std::move(cb)); }, bad);
        ASSERT_EQ(r->statusCode(), k400BadRequest) << bad[0].first << "=" << bad[0].second;
        EXPECT_EQ(body_of(r)["error"], "invalid_query");
    }
}

// A note can be about a deal of the caller's practice; deleting the deal leaves the note, unlinked.
TEST_F(NotesApiTest, ANoteLinksADealOfTheCaller) {
    const auto deal_of = [&](const char* who, const char* title) {
        auto cl = call(
            who, Post, json{{"name", "Acme"}}, [&](auto req, auto cb) { practice.createClient(req, std::move(cb)); });
        EXPECT_EQ(cl->statusCode(), k201Created) << std::string(cl->body());
        auto d =
            call(who, Post, json{{"client_id", body_of(cl)["data"]["id"]}, {"title", title}}, [&](auto req, auto cb) {
                practice.createDeal(req, std::move(cb));
            });
        EXPECT_EQ(d->statusCode(), k201Created) << std::string(d->body());
        return body_of(d)["data"]["id"].get<std::string>();
    };
    const auto site = deal_of(kAnna, "Landing page");
    const auto theirs = deal_of(kBoris, "Their deal");

    auto made = add(kAnna, json{{"title", "Call with Acme"}, {"deal_id", site}});
    ASSERT_EQ(made->statusCode(), k201Created) << std::string(made->body());
    EXPECT_EQ(body_of(made)["data"]["deal_id"], site);
    EXPECT_EQ(body_of(made)["data"]["deal_title"], "Landing page");
    const auto id = body_of(made)["data"]["id"].get<std::string>();
    const auto plain = body_of(add(kAnna, json{{"title", "Unrelated"}}))["data"];
    EXPECT_TRUE(plain["deal_id"].is_null());
    EXPECT_TRUE(plain["deal_title"].is_null());

    auto foreign = add(kAnna, json{{"title", "x"}, {"deal_id", theirs}});
    ASSERT_EQ(foreign->statusCode(), k400BadRequest) << "a deal of someone else";
    EXPECT_EQ(add(kAnna, json{{"title", "x"}, {"deal_id", "not-a-uuid"}})->statusCode(), k400BadRequest);
    auto foreign_edit =
        call(kAnna, Patch, json{{"deal_id", theirs}}, [&](auto req, auto cb) { c.updateNote(req, std::move(cb), id); });
    EXPECT_EQ(foreign_edit->statusCode(), k400BadRequest);

    const json by_deal = list(kAnna, {{"deal_id", site}});
    const json rows = by_deal["data"];
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0]["id"], id);
    EXPECT_EQ(rows[0]["deal_title"], "Landing page") << "the list carries the deal";
    EXPECT_EQ(by_deal["total"], 1);
    auto bad = call(
        kAnna, Get, json::object(), [&](auto req, auto cb) { c.listNotes(req, std::move(cb)); }, {{"deal_id", "nope"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);

    auto unlinked = call(
        kAnna, Patch, json{{"deal_id", nullptr}}, [&](auto req, auto cb) { c.updateNote(req, std::move(cb), id); });
    ASSERT_EQ(unlinked->statusCode(), k200OK) << std::string(unlinked->body());
    TestHelpers::expect_matches_schema(unlinked, "PATCH", "/api/v1/notes/{id}");
    EXPECT_TRUE(body_of(unlinked)["data"]["deal_id"].is_null());
    auto relinked =
        call(kAnna, Patch, json{{"deal_id", site}}, [&](auto req, auto cb) { c.updateNote(req, std::move(cb), id); });
    ASSERT_EQ(relinked->statusCode(), k200OK) << std::string(relinked->body());
    EXPECT_EQ(body_of(relinked)["data"]["deal_id"], site);

    auto gone =
        call(kAnna, Delete, json::object(), [&](auto req, auto cb) { practice.deleteDeal(req, std::move(cb), site); });
    ASSERT_EQ(gone->statusCode(), k204NoContent) << std::string(gone->body());
    auto left = call(kAnna, Get, json::object(), [&](auto req, auto cb) { c.getNote(req, std::move(cb), id); });
    ASSERT_EQ(left->statusCode(), k200OK);
    TestHelpers::expect_matches_schema(left, "GET", "/api/v1/notes/{id}");
    EXPECT_TRUE(body_of(left)["data"]["deal_id"].is_null()) << "the note stays, unlinked";
    EXPECT_TRUE(body_of(left)["data"]["deal_title"].is_null());
}
