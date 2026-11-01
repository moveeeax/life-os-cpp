/**
 * @file test_github_repos_api.cpp
 * @brief The repositories under /api/v1/github/repos: the three views with
 *        their counts and owners, the owner filter, the search over names
 *        and topics, one repository, the 404s of an unlinked user and an
 *        unknown repository, "Not about this" recomputing the attention
 *        list at once, one task per repository and rule (made again only
 *        after the task was closed or deleted), another user's rows out of
 *        sight, a read key allowed to read but not to mute or make a task,
 *        the tasks module off, and the github part of the day's digest
 *        (the Sunday line). The rows are seeded through RepoRepository
 *        straight, as the sync would store them.
 */

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/GithubController.hpp"
#include "api/TodayController.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "github/Rules.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "repositories/github/RepoRepository.hpp"
#include "test_helpers.hpp"
#include "utils/Time.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaa79";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbb79";

using Repo = Repositories::Github::RepoRepository;

Security::Auth::AuthPrincipal principal(const char* who, bool via_key = false) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.via_api_key = via_key;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

using TestHelpers::body_of;

/// An ISO timestamp @p days_ago before now (the pushed_at of a seeded row).
std::string days_ago(int days) {
    return Database::get().execute_read([&](auto& txn) {
        auto r = txn.exec_params(
            "SELECT to_char((now() - make_interval(days => $1)) AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"')",
            days);
        return r[0][0].template as<std::string>();
    });
}

/// A tidy public repository of moveeeax pushed @p days_ago days ago; the tests bend the facts from here.
Repo::Input tidy(const std::string& owner, const std::string& name, int pushed_days_ago) {
    Repo::Input in;
    in.node_id = "R_" + owner + "_" + name;
    in.full_name = owner + "/" + name;
    in.owner = owner;
    in.name = name;
    in.description = "A repository";
    in.license = "MIT";
    in.has_readme = true;
    in.checks = "success";
    in.default_branch = "master";
    in.pushed_at = days_ago(pushed_days_ago);
    return in;
}

/// The verdict of the sync for @p in without mutes, as the job would store it.
json verdict(const Repo::Input& in) {
    Github::RepoFacts f;
    f.full_name = in.full_name;
    f.is_public = in.visibility == "public";
    f.fork = in.fork;
    f.archived = in.archived;
    f.description = in.description;
    f.license = in.license;
    f.has_readme = in.has_readme;
    f.open_issues = in.open_issues;
    f.open_prs = in.open_prs;
    f.open_alerts = in.open_alerts;
    f.checks = in.checks;
    f.default_branch = in.default_branch;
    if (in.pushed_at) {
        f.pushed_at = Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT extract(epoch FROM $1::timestamptz)::bigint", *in.pushed_at);
            return r[0][0].template as<long long>();
        });
    }
    return Github::attention(f, {}, static_cast<long long>(Utils::Time::now_epoch_seconds()));
}

std::vector<std::string> names_of(const json& rows) {
    std::vector<std::string> out;
    for (const auto& r : rows) {
        out.push_back(r["full_name"].get<std::string>());
    }
    return out;
}

std::vector<std::string> rules_of(const json& attention) {
    std::vector<std::string> out;
    for (const auto& item : attention) {
        out.push_back(item["rule"].get<std::string>());
    }
    return out;
}

}  // namespace

class GithubReposApiTest : public TestHelpers::CoreBackedTest {
protected:
    Api::GithubController c;
    Api::TodayController today_c;

    std::string config_file_name() const override { return "github_repos_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["github"]["enabled"] = true;
        cfg["tasks"]["enabled"] = true;
        cfg["now"]["enabled"] = true;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            for (const char* id : {kAnna, kBoris}) {
                const std::string o = id;
                txn.exec_params("DELETE FROM github_mutes WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM github_repos WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM github_links WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM task_items WHERE owner_id = $1::uuid", o);
                txn.exec_params("DELETE FROM day_focus WHERE owner_id = $1::uuid", o);
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

    /// Links @p who and stores the five repositories of the suite:
    ///   moveeeax/life-os-cpp   3 open issues, pushed a day ago        -> issues
    ///   moveeeax/tidy          nothing, pushed two days ago, topic terraform
    ///   cybercapybara/site     one open PR, no license, ten days ago -> prs, hygiene
    ///   moveeeax/old           archived, pushed 400 days ago         -> nothing (archived)
    ///   moveeeax/dusty         private, pushed 400 days ago          -> stale
    void seed(const char* who) {
        Repositories::Github::LinkRepository().set(who, "moveeeax", "sealed", "nonce", "repo read:org");
        std::vector<Repo::Input> rows;
        auto life = tidy("moveeeax", "life-os-cpp", 1);
        life.open_issues = 3;
        life.topics = {"cpp", "life-os"};
        life.stars = 5;
        rows.push_back(life);
        auto tidy_one = tidy("moveeeax", "tidy", 2);
        tidy_one.topics = {"terraform"};
        rows.push_back(tidy_one);
        auto site = tidy("cybercapybara", "site", 10);
        site.open_prs = 1;
        site.license = "";
        rows.push_back(site);
        auto old = tidy("moveeeax", "old", 400);
        old.archived = true;
        rows.push_back(old);
        auto dusty = tidy("moveeeax", "dusty", 400);
        dusty.visibility = "private";
        dusty.has_readme = false;
        rows.push_back(dusty);
        for (auto& r : rows) {
            r.attention = verdict(r);
        }
        Repo().replace(who, rows);
    }

    template <typename Fn>
    HttpResponsePtr call(const Security::Auth::AuthPrincipal& p,
                         HttpMethod method,
                         const json& body,
                         Fn fn,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto req = TestHelpers::authed_json(p, body, method);
        for (const auto& [k, v] : params) {
            req->setParameter(k, v);
        }
        HttpResponsePtr captured;
        fn(req, [&](const HttpResponsePtr& r) { captured = r; });
        EXPECT_TRUE(captured);
        return captured;
    }

    HttpResponsePtr list(const Security::Auth::AuthPrincipal& p,
                         const std::vector<std::pair<std::string, std::string>>& params = {}) {
        auto r = call(
            p, Get, json::object(), [&](auto req, auto cb) { c.repos(req, std::move(cb)); }, params);
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/github/repos");
        return r;
    }

    HttpResponsePtr one(const Security::Auth::AuthPrincipal& p, const std::string& owner, const std::string& name) {
        auto r = call(p, Get, json::object(), [&](auto req, auto cb) { c.repo(req, std::move(cb), owner, name); });
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/github/repos/{owner}/{name}");
        return r;
    }

    HttpResponsePtr mute(const Security::Auth::AuthPrincipal& p,
                         const std::string& owner,
                         const std::string& name,
                         const std::string& rule) {
        auto r =
            call(p, Put, json::object(), [&](auto req, auto cb) { c.mute(req, std::move(cb), owner, name, rule); });
        TestHelpers::expect_2xx_matches_schema(r, "PUT", "/api/v1/github/repos/{owner}/{name}/mutes/{rule}");
        return r;
    }

    HttpResponsePtr unmute(const Security::Auth::AuthPrincipal& p,
                           const std::string& owner,
                           const std::string& name,
                           const std::string& rule) {
        auto r = call(
            p, Delete, json::object(), [&](auto req, auto cb) { c.unmute(req, std::move(cb), owner, name, rule); });
        TestHelpers::expect_2xx_matches_schema(r, "DELETE", "/api/v1/github/repos/{owner}/{name}/mutes/{rule}");
        return r;
    }

    HttpResponsePtr task(const Security::Auth::AuthPrincipal& p,
                         const std::string& owner,
                         const std::string& name,
                         const json& body) {
        auto r = call(p, Post, body, [&](auto req, auto cb) { c.makeTask(req, std::move(cb), owner, name); });
        TestHelpers::expect_2xx_matches_schema(r, "POST", "/api/v1/github/repos/{owner}/{name}/task");
        return r;
    }

    json today(const char* who) {
        auto r =
            call(principal(who), Get, json::object(), [&](auto req, auto cb) { today_c.today(req, std::move(cb)); });
        EXPECT_EQ(r->statusCode(), k200OK) << std::string(r->body());
        TestHelpers::expect_2xx_matches_schema(r, "GET", "/api/v1/today");
        return body_of(r)["data"];
    }

    json task_row(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT json_build_object('title', title, 'note', note, 'status', status, 'source_kind', "
                "source_kind, 'source_ref', source_ref, 'due', due)::text FROM task_items WHERE id = $1::uuid",
                id);
            return json::parse(r[0][0].template as<std::string>());
        });
    }
};

TEST_F(GithubReposApiTest, NotLinkedIs404) {
    auto r = list(principal(kAnna));
    EXPECT_EQ(r->statusCode(), k404NotFound);
    EXPECT_NE(body_of(r)["message"].get<std::string>().find("github_not_linked"), std::string::npos);
    auto o = one(principal(kAnna), "moveeeax", "life-os-cpp");
    EXPECT_EQ(o->statusCode(), k404NotFound);
    EXPECT_NE(body_of(o)["message"].get<std::string>().find("github_not_linked"), std::string::npos);
    auto m = mute(principal(kAnna), "moveeeax", "life-os-cpp", "issues");
    EXPECT_EQ(m->statusCode(), k404NotFound);
    auto t = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    EXPECT_EQ(t->statusCode(), k404NotFound);
}

TEST_F(GithubReposApiTest, ViewsCountsAndOwners) {
    seed(kAnna);
    auto r = list(principal(kAnna));
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const json page = body_of(r);
    // The default view is attention: last push first, the archived one left out.
    EXPECT_EQ(names_of(page["data"]),
              (std::vector<std::string>{"moveeeax/life-os-cpp", "cybercapybara/site", "moveeeax/dusty"}));
    EXPECT_EQ(page["total"], 3);
    EXPECT_EQ(page["limit"], 50);
    EXPECT_EQ(page["offset"], 0);
    EXPECT_EQ(page["owners"], (json{"cybercapybara", "moveeeax"}));
    EXPECT_EQ(page["counts"], (json{{"attention", 3}, {"all", 4}, {"archived", 1}}));
    EXPECT_EQ(rules_of(page["data"][0]["attention"]), (std::vector<std::string>{"issues"}));
    EXPECT_EQ(page["data"][0]["attention"][0]["detail"], "3 open issues");
    EXPECT_EQ(rules_of(page["data"][1]["attention"]), (std::vector<std::string>{"prs", "hygiene"}));
    EXPECT_EQ(rules_of(page["data"][2]["attention"]), (std::vector<std::string>{"stale"}));

    const json all = body_of(list(principal(kAnna), {{"view", "all"}}));
    EXPECT_EQ(
        names_of(all["data"]),
        (std::vector<std::string>{"moveeeax/life-os-cpp", "moveeeax/tidy", "cybercapybara/site", "moveeeax/dusty"}));
    EXPECT_EQ(all["total"], 4);

    const json archived = body_of(list(principal(kAnna), {{"view", "archived"}}));
    EXPECT_EQ(names_of(archived["data"]), (std::vector<std::string>{"moveeeax/old"}));
    EXPECT_TRUE(archived["data"][0]["attention"].empty());

    auto bad = list(principal(kAnna), {{"view", "mine"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(bad)["error"], "invalid_query");
}

TEST_F(GithubReposApiTest, OwnerFilterSearchAndPaging) {
    seed(kAnna);
    const json org = body_of(list(principal(kAnna), {{"view", "all"}, {"owner", "cybercapybara"}}));
    EXPECT_EQ(names_of(org["data"]), (std::vector<std::string>{"cybercapybara/site"}));
    EXPECT_EQ(org["total"], 1);
    // The owners and the counts stay those of every repository: the chips and the tabs do not shrink.
    EXPECT_EQ(org["owners"], (json{"cybercapybara", "moveeeax"}));
    EXPECT_EQ(org["counts"]["all"], 4);

    // q over the full name and over the topics, case-insensitive.
    const json by_topic = body_of(list(principal(kAnna), {{"view", "all"}, {"q", "Terraform"}}));
    EXPECT_EQ(names_of(by_topic["data"]), (std::vector<std::string>{"moveeeax/tidy"}));
    const json by_name = body_of(list(principal(kAnna), {{"view", "all"}, {"q", "life-os"}}));
    EXPECT_EQ(names_of(by_name["data"]), (std::vector<std::string>{"moveeeax/life-os-cpp"}));
    const json none = body_of(list(principal(kAnna), {{"view", "all"}, {"q", "%"}}));
    EXPECT_EQ(none["total"], 0);

    const json page = body_of(list(principal(kAnna), {{"view", "all"}, {"limit", "2"}, {"offset", "2"}}));
    EXPECT_EQ(names_of(page["data"]), (std::vector<std::string>{"cybercapybara/site", "moveeeax/dusty"}));
    EXPECT_EQ(page["total"], 4);
    EXPECT_EQ(page["limit"], 2);
    EXPECT_EQ(page["offset"], 2);
}

TEST_F(GithubReposApiTest, OneRepository) {
    seed(kAnna);
    auto r = one(principal(kAnna), "moveeeax", "life-os-cpp");
    ASSERT_EQ(r->statusCode(), k200OK) << std::string(r->body());
    const json row = body_of(r)["data"];
    EXPECT_EQ(row["full_name"], "moveeeax/life-os-cpp");
    EXPECT_EQ(row["owner"], "moveeeax");
    EXPECT_EQ(row["name"], "life-os-cpp");
    EXPECT_EQ(row["visibility"], "public");
    EXPECT_EQ(row["topics"], (json{"cpp", "life-os"}));
    EXPECT_EQ(row["stars"], 5);
    EXPECT_EQ(row["license"], "MIT");
    EXPECT_EQ(row["open_issues"], 3);
    EXPECT_EQ(row["muted"], json::array());
    EXPECT_EQ(row["tasks"], json::object());
    EXPECT_FALSE(row["pushed_at"].is_null());

    auto missing = one(principal(kAnna), "moveeeax", "nope");
    EXPECT_EQ(missing->statusCode(), k404NotFound);
    EXPECT_NE(body_of(missing)["message"].get<std::string>().find("github_repo_not_found"), std::string::npos);
}

TEST_F(GithubReposApiTest, MuteRecomputesAttentionAtOnce) {
    seed(kAnna);
    auto m = mute(principal(kAnna), "cybercapybara", "site", "hygiene");
    ASSERT_EQ(m->statusCode(), k200OK) << std::string(m->body());
    EXPECT_EQ(body_of(m)["data"]["muted"], (json{"hygiene"}));
    EXPECT_EQ(rules_of(body_of(m)["data"]["attention"]), (std::vector<std::string>{"prs"}));
    // Idempotent.
    auto again = mute(principal(kAnna), "cybercapybara", "site", "hygiene");
    EXPECT_EQ(again->statusCode(), k200OK);
    EXPECT_EQ(body_of(again)["data"]["muted"], (json{"hygiene"}));

    // The only item muted: the repository leaves the attention view and its count at once.
    auto only = mute(principal(kAnna), "moveeeax", "life-os-cpp", "issues");
    ASSERT_EQ(only->statusCode(), k200OK);
    EXPECT_TRUE(body_of(only)["data"]["attention"].empty());
    const json page = body_of(list(principal(kAnna)));
    EXPECT_EQ(names_of(page["data"]), (std::vector<std::string>{"cybercapybara/site", "moveeeax/dusty"}));
    EXPECT_EQ(page["counts"]["attention"], 2);

    // Unmuted: the rule speaks again; unmuting what was never muted is fine too.
    auto u = unmute(principal(kAnna), "moveeeax", "life-os-cpp", "issues");
    ASSERT_EQ(u->statusCode(), k200OK);
    EXPECT_EQ(body_of(u)["data"]["muted"], json::array());
    EXPECT_EQ(rules_of(body_of(u)["data"]["attention"]), (std::vector<std::string>{"issues"}));
    auto u2 = unmute(principal(kAnna), "moveeeax", "tidy", "stale");
    EXPECT_EQ(u2->statusCode(), k200OK);
    EXPECT_EQ(body_of(list(principal(kAnna)))["counts"]["attention"], 3);

    auto bad = mute(principal(kAnna), "moveeeax", "life-os-cpp", "vibes");
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(bad)["error"], "invalid_rule");
    auto missing = mute(principal(kAnna), "moveeeax", "nope", "issues");
    EXPECT_EQ(missing->statusCode(), k404NotFound);
    EXPECT_NE(body_of(missing)["message"].get<std::string>().find("github_repo_not_found"), std::string::npos);
}

TEST_F(GithubReposApiTest, OneTaskPerRepositoryAndRule) {
    seed(kAnna);
    auto first = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    ASSERT_EQ(first->statusCode(), k201Created) << std::string(first->body());
    EXPECT_EQ(body_of(first)["data"]["created"], true);
    const std::string id = body_of(first)["data"]["task_id"].get<std::string>();
    const json row = task_row(id);
    EXPECT_EQ(row["title"], "moveeeax/life-os-cpp: 3 open issues");
    EXPECT_NE(row["note"].get<std::string>().find("https://github.com/moveeeax/life-os-cpp"), std::string::npos);
    EXPECT_NE(row["note"].get<std::string>().find("issues"), std::string::npos);
    EXPECT_EQ(row["source_kind"], "url");
    EXPECT_EQ(row["source_ref"], "https://github.com/moveeeax/life-os-cpp");
    EXPECT_TRUE(row["due"].is_null());
    EXPECT_EQ(row["status"], "open");
    EXPECT_EQ(body_of(one(principal(kAnna), "moveeeax", "life-os-cpp"))["data"]["tasks"]["issues"], id);

    // The same rule again: the task made before, not a second one.
    auto second = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    ASSERT_EQ(second->statusCode(), k201Created);
    EXPECT_EQ(body_of(second)["data"]["created"], false);
    EXPECT_EQ(body_of(second)["data"]["task_id"], id);

    // Another rule, also one not in the verdict, makes its own task with the rule's words.
    auto other = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "hygiene"}});
    ASSERT_EQ(other->statusCode(), k201Created);
    EXPECT_EQ(body_of(other)["data"]["created"], true);
    EXPECT_NE(body_of(other)["data"]["task_id"], id);
    EXPECT_EQ(task_row(body_of(other)["data"]["task_id"].get<std::string>())["title"],
              "moveeeax/life-os-cpp: description, license or README");

    // Closed: the next tap makes a new one; deleted: the same.
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("UPDATE task_items SET status = 'done', completed_at = now() WHERE id = $1::uuid", id);
        return true;
    });
    auto third = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    ASSERT_EQ(third->statusCode(), k201Created);
    EXPECT_EQ(body_of(third)["data"]["created"], true);
    const std::string id2 = body_of(third)["data"]["task_id"].get<std::string>();
    EXPECT_NE(id2, id);
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params("DELETE FROM task_items WHERE id = $1::uuid", id2);
        return true;
    });
    auto fourth = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    ASSERT_EQ(fourth->statusCode(), k201Created);
    EXPECT_EQ(body_of(fourth)["data"]["created"], true);
    EXPECT_NE(body_of(fourth)["data"]["task_id"], id2);

    auto bad = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "vibes"}});
    EXPECT_EQ(bad->statusCode(), k400BadRequest);
    EXPECT_EQ(body_of(bad)["error"], "invalid_rule");
    auto no_rule = task(principal(kAnna), "moveeeax", "life-os-cpp", json::object());
    EXPECT_EQ(no_rule->statusCode(), k400BadRequest);
    auto missing = task(principal(kAnna), "moveeeax", "nope", json{{"rule", "issues"}});
    EXPECT_EQ(missing->statusCode(), k404NotFound);
    EXPECT_NE(body_of(missing)["message"].get<std::string>().find("github_repo_not_found"), std::string::npos);
}

TEST_F(GithubReposApiTest, AnotherUsersRowsStayOutOfSight) {
    seed(kAnna);
    Repositories::Github::LinkRepository().set(kBoris, "boris", "sealed", "nonce", "repo");
    const json page = body_of(list(principal(kBoris), {{"view", "all"}}));
    EXPECT_EQ(page["total"], 0);
    EXPECT_EQ(page["owners"], json::array());
    EXPECT_EQ(page["counts"], (json{{"attention", 0}, {"all", 0}, {"archived", 0}}));
    EXPECT_EQ(one(principal(kBoris), "moveeeax", "life-os-cpp")->statusCode(), k404NotFound);
    EXPECT_EQ(mute(principal(kBoris), "moveeeax", "life-os-cpp", "issues")->statusCode(), k404NotFound);
    EXPECT_EQ(task(principal(kBoris), "moveeeax", "life-os-cpp", json{{"rule", "issues"}})->statusCode(), k404NotFound);
    // Anna's row is as it was.
    EXPECT_EQ(body_of(one(principal(kAnna), "moveeeax", "life-os-cpp"))["data"]["muted"], json::array());
}

TEST_F(GithubReposApiTest, ReadKeyReadsButNeitherMutesNorMakesTasks) {
    seed(kAnna);
    const auto key = principal(kAnna, true);
    EXPECT_EQ(list(key)->statusCode(), k200OK);
    EXPECT_EQ(one(key, "moveeeax", "life-os-cpp")->statusCode(), k200OK);
    auto m = mute(key, "moveeeax", "life-os-cpp", "issues");
    EXPECT_EQ(m->statusCode(), k401Unauthorized);
    EXPECT_EQ(body_of(m)["error"], "key_not_allowed");
    auto u = unmute(key, "moveeeax", "life-os-cpp", "issues");
    EXPECT_EQ(u->statusCode(), k401Unauthorized);
    auto t = task(key, "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    EXPECT_EQ(t->statusCode(), k401Unauthorized);
    EXPECT_EQ(body_of(t)["error"], "key_not_allowed");
    EXPECT_EQ(body_of(one(principal(kAnna), "moveeeax", "life-os-cpp"))["data"]["muted"], json::array());
}

TEST_F(GithubReposApiTest, TodayCarriesTheSundayLine) {
    seed(kAnna);
    const json anna = today(kAnna);
    ASSERT_TRUE(anna.contains("github")) << anna.dump();
    EXPECT_EQ(anna["github"]["attention"], 3);
    EXPECT_EQ(anna["github"]["top"], (json{"moveeeax/life-os-cpp", "cybercapybara/site"}));
    // Nothing needing attention, or no link at all: no github part.
    Repositories::Github::LinkRepository().set(kBoris, "boris", "sealed", "nonce", "repo");
    EXPECT_FALSE(today(kBoris).contains("github"));
    mute(principal(kAnna), "moveeeax", "life-os-cpp", "issues");
    mute(principal(kAnna), "cybercapybara", "site", "prs");
    mute(principal(kAnna), "cybercapybara", "site", "hygiene");
    mute(principal(kAnna), "moveeeax", "dusty", "stale");
    EXPECT_FALSE(today(kAnna).contains("github"));
}

class GithubReposNoTasksApiTest : public GithubReposApiTest {
protected:
    std::string config_file_name() const override { return "github_repos_no_tasks_api_test_config.json"; }
    void config_overrides(nlohmann::json& cfg) override {
        cfg["github"]["enabled"] = true;
        cfg["tasks"]["enabled"] = false;
        cfg["now"]["enabled"] = true;
    }
};

TEST_F(GithubReposNoTasksApiTest, TasksModuleOffIs404) {
    seed(kAnna);
    auto t = task(principal(kAnna), "moveeeax", "life-os-cpp", json{{"rule", "issues"}});
    EXPECT_EQ(t->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(t)["message"], "tasks not found");
    EXPECT_EQ(one(principal(kAnna), "moveeeax", "life-os-cpp")->statusCode(), k200OK);
}
