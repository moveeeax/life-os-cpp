/**
 * @file test_github_sync.cpp
 * @brief The github_sync job against a real database and a scripted GitHub:
 *        a sync stores every repository with its verdict after the user's
 *        mutes and the org hint, a repository gone from GitHub goes at the
 *        next sync, a refusal keeps the old list and names itself in
 *        sync_error, a running sync is not run twice, and the tick queues
 *        one job per user whose sync is due.
 */

#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "github/Settings.hpp"
#include "github/jobs/Sync.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "repositories/github/MuteRepository.hpp"
#include "repositories/github/RepoRepository.hpp"
#include "test_helpers.hpp"
#include "utils/Seal.hpp"

namespace {

/// Every repository of a user, the first page.
const Repositories::Github::RepoRepository::Filter kEvery = [] {
    Repositories::Github::RepoRepository::Filter f;
    f.view = "all";
    return f;
}();

using nlohmann::json;
using Repositories::Github::LinkRepository;
using Repositories::Github::MuteRepository;
using Repositories::Github::RepoRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0972";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0972";
const std::string kKey = Utils::Seal::new_key();

json node(const std::string& id, const std::string& owner, const std::string& name) {
    return json{{"id", id},
                {"name", name},
                {"nameWithOwner", owner + "/" + name},
                {"owner", {{"login", owner}}},
                {"visibility", "PUBLIC"},
                {"isFork", false},
                {"isArchived", false},
                {"description", "A thing"},
                {"repositoryTopics", {{"nodes", json::array()}}},
                {"licenseInfo", {{"spdxId", "MIT"}}},
                {"stargazerCount", 1},
                {"pushedAt", "2026-10-29T11:00:00Z"},
                {"defaultBranchRef", {{"name", "main"}, {"target", {{"statusCheckRollup", nullptr}}}}},
                {"readmeMd", {{"id", "b"}}},
                {"readmePlain", nullptr},
                {"readmeRst", nullptr},
                {"issues", {{"totalCount", 0}}},
                {"pullRequests", {{"totalCount", 0}}},
                {"vulnerabilityAlerts", {{"totalCount", 0}}}};
}

/// GitHub as scripted: the viewer owns @p mine, belongs to cybercapybara (one repository) and locked (forbidden).
struct ScriptedGitHub : Net::Http::Transport {
    std::vector<json> mine;
    long status = 200;
    int calls = 0;
    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        return {404, ""};
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        ++calls;
        if (status != 200) {
            return {status, R"({"message":"no"})"};
        }
        const json q = json::parse(body);
        const std::string query = q.value("query", "");
        const json vars = q.value("variables", json::object());
        const auto page = [](const std::vector<json>& nodes) {
            return json{{"pageInfo", {{"hasNextPage", false}, {"endCursor", nullptr}}}, {"nodes", nodes}};
        };
        if (query.find("organization(login:") != std::string::npos) {
            if (vars.value("login", "") == "locked") {
                return {200,
                        json{{"data", {{"organization", nullptr}}},
                             {"errors", json::array({json{{"type", "FORBIDDEN"}, {"message", "restricted"}}})}}
                            .dump()};
            }
            return {200,
                    json{{"data", {{"organization", {{"repositories", page({node("o1", "cybercapybara", "bot")})}}}}}}
                        .dump()};
        }
        json viewer{
            {"login", "moveeeax"},
            {"organizations", {{"nodes", json::array({json{{"login", "cybercapybara"}}, json{{"login", "locked"}}})}}},
            {"repositories", page(mine)}};
        return {200, json{{"data", {{"viewer", viewer}}}}.dump()};
    }
};

class GithubSyncTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedGitHub github;
    LinkRepository links;
    RepoRepository repos;

    std::string config_file_name() const override { return "github_sync_test_config.json"; }
    void config_overrides(json& cfg) override {
        cfg["github"]["enabled"] = true;
        cfg["github"]["client_id"] = "cid123";
        cfg["github"]["client_secret"] = "sec";
        cfg["github"]["redirect_url"] = "https://life.test/api/v1/github/oauth/callback";
        cfg["github"]["token_key"] = kKey;
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM github_mutes");
            txn.exec("DELETE FROM github_repos");
            txn.exec("DELETE FROM github_links");
            return true;
        });
        for (const char* id : {kAnna, kBoris}) {
            Cache::get().del(Github::lock_key(id));
        }
        TestHelpers::drain_jobs({Github::kSyncJob});
        github.mine = {node("r1", "moveeeax", "first"), node("r2", "moveeeax", "second")};
        Net::Http::install_for_testing(&github);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::drain_jobs({Github::kSyncJob});
        TestHelpers::CoreBackedTest::TearDown();
    }

    void linked(const char* who, const std::string& token = "gho_x") {
        const auto sealed = Utils::Seal::seal(token, kKey);
        links.set(who, "moveeeax", sealed.ciphertext, sealed.nonce, "repo read:org");
    }

    static json run(const char* who) { return Github::process_sync(json{{"owner", who}}); }

    static long queued() {
        return static_cast<long>(Cache::get().get_client().llen(Jobs::queue_key(Github::kSyncJob)));
    }
};

}  // namespace

TEST_F(GithubSyncTest, SyncStoresRepositoriesVerdictsAndTheOrgHint) {
    linked(kAnna);
    github.mine[1]["issues"]["totalCount"] = 3;
    github.mine[1]["defaultBranchRef"]["target"]["statusCheckRollup"] = {{"state", "FAILURE"}};
    MuteRepository().put(kAnna, "moveeeax/second", "issues");
    const json out = run(kAnna);
    EXPECT_EQ(out["status"], "done") << out.dump();
    EXPECT_EQ(out["repos"], 3);

    const auto all = repos.list(kAnna, kEvery);
    EXPECT_EQ(all["total"], 3);
    EXPECT_EQ(all["owners"], json::array({"cybercapybara", "moveeeax"}));
    const auto second = repos.one(kAnna, "moveeeax/second");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ((*second)["open_issues"], 3);
    EXPECT_EQ((*second)["checks"], "failure");
    ASSERT_EQ((*second)["attention"].size(), 1u) << (*second)["attention"].dump();
    EXPECT_EQ((*second)["attention"][0]["rule"], "checks") << "issues is muted";
    EXPECT_EQ((*second)["muted"], json::array({"issues"}));
    const auto first = repos.one(kAnna, "moveeeax/first");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ((*first)["attention"], json::array()) << "nothing to do";
    EXPECT_EQ((*first)["pushed_at"], "2026-10-29T11:00:00Z");

    const auto link = links.get(kAnna);
    ASSERT_TRUE(link.has_value());
    EXPECT_TRUE(link->synced_at.has_value());
    EXPECT_FALSE(link->sync_error.has_value());
    EXPECT_EQ(link->org_hint, "locked") << "the organisation that refused the app";
    EXPECT_EQ(repos.list(kBoris, kEvery)["total"], 0) << "per user";
}

TEST_F(GithubSyncTest, ARepositoryGoneFromGithubGoesAtTheNextSync) {
    linked(kAnna);
    run(kAnna);
    EXPECT_EQ(repos.list(kAnna, kEvery)["total"], 3);
    github.mine = {node("r1", "moveeeax", "renamed")};
    run(kAnna);
    EXPECT_EQ(repos.list(kAnna, kEvery)["total"], 2);
    EXPECT_TRUE(repos.one(kAnna, "moveeeax/renamed").has_value()) << "the node id keeps the row through a rename";
    EXPECT_FALSE(repos.one(kAnna, "moveeeax/second").has_value());
}

TEST_F(GithubSyncTest, ARefusalKeepsTheOldListAndNamesItself) {
    linked(kAnna);
    run(kAnna);
    github.status = 401;
    const json out = run(kAnna);
    EXPECT_EQ(out["status"], "failed");
    EXPECT_EQ(out["error"], "token_revoked");
    EXPECT_EQ(repos.list(kAnna, kEvery)["total"], 3) << "the last list stays";
    const auto link = links.get(kAnna);
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ(link->sync_error, "token_revoked");
    EXPECT_TRUE(link->synced_at.has_value()) << "synced_at is the last sync that worked";
    github.status = 429;
    EXPECT_EQ(run(kAnna)["error"], "rate_limited");
}

TEST_F(GithubSyncTest, ATokenThatDoesNotOpenIsAFailure) {
    links.set(kAnna, "moveeeax", "not-a-ciphertext", "nonce", "repo");
    EXPECT_EQ(run(kAnna)["error"], "token_unreadable");
    EXPECT_EQ(github.calls, 0);
    EXPECT_EQ(run(kBoris)["status"], "skipped") << "not linked";
}

TEST_F(GithubSyncTest, OneSyncPerOwnerAtATime) {
    linked(kAnna);
    ASSERT_TRUE(Cache::get().set_nx(Github::lock_key(kAnna), "1", std::chrono::seconds(60)));
    const json out = run(kAnna);
    EXPECT_EQ(out["status"], "skipped");
    EXPECT_EQ(out["reason"], "running");
    EXPECT_EQ(github.calls, 0);
    Cache::get().del(Github::lock_key(kAnna));
    EXPECT_EQ(run(kAnna)["status"], "done");
    EXPECT_FALSE(Cache::get().exists(Github::lock_key(kAnna))) << "released after the sync";
}

TEST_F(GithubSyncTest, TheTickQueuesOneJobPerDueUser) {
    linked(kAnna);
    linked(kBoris);
    links.mark_synced(kBoris, "");
    json out = Github::process_sync(json::object());
    EXPECT_EQ(out["status"], "queued");
    EXPECT_EQ(out["owners"], 1) << "anna never synced, boris just did";
    EXPECT_EQ(queued(), 1);
    Database::get().execute_write([](auto& txn) {
        txn.exec_params("UPDATE github_links SET synced_at = now() - interval '7 hours' WHERE owner_id = $1::uuid",
                        std::string(kBoris));
        return true;
    });
    EXPECT_EQ(Github::process_sync(json::object())["owners"], 2) << "older than six hours is due";
    links.mark_failed(kAnna, "token_revoked");
    EXPECT_EQ(Github::process_sync(json::object())["owners"], 1) << "a failure within the hour waits";
}
