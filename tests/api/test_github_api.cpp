/**
 * @file test_github_api.cpp
 * @brief The GitHub link routes over a scripted GitHub: the status before and
 *        after a link, the authorize redirect with a one-time state, the
 *        callback (a wrong state, a denial, a scripted exchange that seals the
 *        token and queues the first sync), "Sync now" with its minute, the
 *        unlink, and the API keys the link routes refuse.
 */

#include <string>
#include <utility>
#include <vector>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/GithubController.hpp"
#include "cache/Cache.hpp"
#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "github/Settings.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "repositories/github/LinkRepository.hpp"
#include "repositories/github/MuteRepository.hpp"
#include "repositories/github/RepoRepository.hpp"
#include "security/Jwt.hpp"
#include "test_helpers.hpp"
#include "utils/Seal.hpp"
#include "utils/Time.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaa0971";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbb0971";
const std::string kKey = Utils::Seal::new_key();

using TestHelpers::body_of;

Security::Auth::AuthPrincipal principal(const char* who, bool via_key = false) {
    Security::Auth::AuthPrincipal p;
    p.subject = who;
    p.via_api_key = via_key;
    p.raw_claims = json{{"sub", who}, {"permissions", Domain::Permission::kGeneral}};
    return p;
}

/// GitHub as scripted: the token endpoint and the viewer query.
struct ScriptedGitHub : Net::Http::Transport {
    bool refuse_code = false;
    int exchanges = 0;
    int revokes = 0;
    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        return {404, ""};
    }
    Net::Http::Response post_json(const std::string& url,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        if (url == std::string(Github::kGraphqlUrl)) {
            return {200, R"({"data":{"viewer":{"login":"moveeeax"}}})"};
        }
        return {404, ""};
    }
    Net::Http::Response send(const Net::Http::Request& r) override {
        if (r.url == std::string(Github::kTokenUrl)) {
            ++exchanges;
            if (refuse_code) {
                return {200, R"({"error":"bad_verification_code"})"};
            }
            return {200, R"({"access_token":"gho_secret","scope":"repo,read:org","token_type":"bearer"})"};
        }
        if (r.url.find("/applications/") != std::string::npos) {
            ++revokes;
            return {204, ""};
        }
        return {404, ""};
    }
};

class GithubApiBase : public TestHelpers::CoreBackedTest {
protected:
    ScriptedGitHub github;
    Api::GithubController controller;
    Repositories::Github::LinkRepository links;

    void config_overrides(json& cfg) override {
        cfg["github"]["enabled"] = true;
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
            for (const char* id : {kAnna, kBoris}) {
                txn.exec_params(
                    "INSERT INTO users (id, email, confirmed, role_id) VALUES ($1::uuid, $2, TRUE, (SELECT id FROM "
                    "roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
                    std::string(id),
                    std::string(id) + "@example.test");
            }
            return true;
        });
        for (const char* id : {kAnna, kBoris}) {
            Cache::get().del(std::string("github:sync:") + id);
        }
        TestHelpers::drain_jobs({Github::kSyncJob});
        Net::Http::install_for_testing(&github);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::drain_jobs({Github::kSyncJob});
        TestHelpers::CoreBackedTest::TearDown();
    }

    HttpResponsePtr status(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.status(TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr start(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.oauthStart(TestHelpers::authed(p, Get), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    /// The callback as the browser of @p who (an access token in the Authorization header, which the cookie
    /// extractor also reads) comes back with @p state and @p code.
    HttpResponsePtr callback(const char* who,
                             const std::string& state,
                             const std::string& code,
                             const std::string& error = "") {
        auto req = TestHelpers::make_request(Get);
        req->setParameter("state", state);
        if (!code.empty()) {
            req->setParameter("code", code);
        }
        if (!error.empty()) {
            req->setParameter("error", error);
        }
        if (who != nullptr) {
            const auto now_ms = Utils::Time::now_epoch_millis();
            const json claims = Security::Auth::access_claims(
                Security::Auth::AccessSubject{who, true, Domain::Permission::kGeneral, {}},
                now_ms,
                600,
                "roles",
                "",
                "");
            req->addHeader("Authorization",
                           "Bearer " + Security::Auth::issue_hs256_jwt(claims, TestHelpers::kTestJwtSecret));
        }
        TestHelpers::AwaitedResponse awaited;
        controller.oauthCallback(req, awaited.callback());
        return awaited.get();
    }

    HttpResponsePtr sync(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.sync(TestHelpers::authed(p, Post), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    HttpResponsePtr disconnect(const Security::Auth::AuthPrincipal& p) {
        HttpResponsePtr captured;
        controller.disconnect(TestHelpers::authed(p, Delete), [&](const HttpResponsePtr& r) { captured = r; });
        return captured;
    }

    void linked(const char* who) {
        const auto sealed = Utils::Seal::seal("gho_old", kKey);
        links.set(who, "moveeeax", sealed.ciphertext, sealed.nonce, "repo read:org");
    }

    static std::string location(const HttpResponsePtr& r) { return r->getHeader("Location"); }

    static long queued() {
        return static_cast<long>(Cache::get().get_client().llen(Jobs::queue_key(Github::kSyncJob)));
    }
};

class GithubApiTest : public GithubApiBase {
protected:
    std::string config_file_name() const override { return "github_api_test_config.json"; }
    void config_overrides(json& cfg) override {
        GithubApiBase::config_overrides(cfg);
        cfg["github"]["client_id"] = "cid123";
        cfg["github"]["client_secret"] = "sec";
        cfg["github"]["redirect_url"] = "https://life.test/api/v1/github/oauth/callback";
        cfg["github"]["token_key"] = kKey;
    }
};

/// The module on, the OAuth App not set up.
class GithubApiUnconfiguredTest : public GithubApiBase {
protected:
    std::string config_file_name() const override { return "github_api_unconfigured_test_config.json"; }
};

}  // namespace

TEST_F(GithubApiUnconfiguredTest, NotConfiguredIsVisibleAndStartIs404) {
    const auto anna = principal(kAnna);
    const auto st = status(anna);
    ASSERT_EQ(st->statusCode(), k200OK) << st->body();
    TestHelpers::expect_matches_schema(st, "GET", "/api/v1/github");
    EXPECT_FALSE(body_of(st)["data"]["configured"].get<bool>());
    EXPECT_FALSE(body_of(st)["data"]["linked"].get<bool>());
    const auto resp = start(anna);
    ASSERT_EQ(resp->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(resp)["error"], "github_not_configured");
    EXPECT_EQ(location(callback(kAnna, "whatever", "c")), "/account?github=error");
}

TEST_F(GithubApiTest, StatusBeforeAndAfterALink) {
    const auto anna = principal(kAnna);
    auto st = status(anna);
    ASSERT_EQ(st->statusCode(), k200OK) << st->body();
    TestHelpers::expect_matches_schema(st, "GET", "/api/v1/github");
    json data = body_of(st)["data"];
    EXPECT_TRUE(data["configured"].get<bool>());
    EXPECT_FALSE(data["linked"].get<bool>());
    EXPECT_TRUE(data["login"].is_null());
    EXPECT_EQ(data["scopes"], json::array());

    linked(kAnna);
    st = status(anna);
    TestHelpers::expect_matches_schema(st, "GET", "/api/v1/github");
    data = body_of(st)["data"];
    EXPECT_TRUE(data["linked"].get<bool>());
    EXPECT_EQ(data["login"], "moveeeax");
    EXPECT_EQ(data["scopes"], json::array({"repo", "read:org"}));
    EXPECT_TRUE(data["synced_at"].is_null());
    EXPECT_TRUE(data["sync_error"].is_null());
    EXPECT_TRUE(data["org_hint"].is_null());
    EXPECT_FALSE(body_of(status(principal(kBoris)))["data"]["linked"].get<bool>()) << "per user";
}

TEST_F(GithubApiTest, StartRedirectsWithAOneTimeState) {
    const auto resp = start(principal(kAnna));
    ASSERT_EQ(resp->statusCode(), k302Found) << resp->body();
    const std::string url = location(resp);
    EXPECT_EQ(url.rfind("https://github.com/login/oauth/authorize?", 0), 0u);
    EXPECT_NE(url.find("client_id=cid123"), std::string::npos);
    EXPECT_NE(url.find("scope=repo%20read%3Aorg"), std::string::npos);
    const auto at = url.find("state=");
    ASSERT_NE(at, std::string::npos);
    const std::string state = url.substr(at + 6);
    EXPECT_EQ(links.take_state(state, kAnna), kAnna);
    EXPECT_FALSE(links.take_state(state, kAnna).has_value()) << "one use";
}

TEST_F(GithubApiTest, ApiKeysAreRefusedOnLinkAndSync) {
    const auto key = principal(kAnna, true);
    for (const auto& resp : {start(key), sync(key), disconnect(key)}) {
        ASSERT_EQ(resp->statusCode(), k401Unauthorized) << resp->body();
        EXPECT_EQ(body_of(resp)["error"], "key_not_allowed");
    }
    EXPECT_EQ(status(key)->statusCode(), k200OK) << "a read key may read the status";
}

TEST_F(GithubApiTest, CallbackWithAWrongOrForeignStateExpires) {
    EXPECT_EQ(location(callback(kAnna, "unknown", "c")), "/account?github=expired");
    const std::string state = links.new_state(kAnna);
    EXPECT_EQ(location(callback(kBoris, state, "c")), "/account?github=expired") << "bound to the user";
    EXPECT_EQ(location(callback(nullptr, links.new_state(kAnna), "c")), "/account?github=expired")
        << "no session, no link";
    EXPECT_EQ(github.exchanges, 0);
    EXPECT_FALSE(links.get(kAnna).has_value());
}

TEST_F(GithubApiTest, CallbackDeniedOrWithoutCode) {
    EXPECT_EQ(location(callback(kAnna, links.new_state(kAnna), "", "access_denied")), "/account?github=denied");
    EXPECT_EQ(location(callback(kAnna, links.new_state(kAnna), "")), "/account?github=denied");
    EXPECT_EQ(github.exchanges, 0);
}

TEST_F(GithubApiTest, CallbackExchangesSealsAndQueuesTheFirstSync) {
    const auto resp = callback(kAnna, links.new_state(kAnna), "the-code");
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(location(resp), "/account?github=ok");
    EXPECT_EQ(github.exchanges, 1);
    const auto link = links.get(kAnna);
    ASSERT_TRUE(link.has_value());
    EXPECT_EQ(link->login, "moveeeax");
    EXPECT_EQ(link->scopes, "repo read:org");
    EXPECT_EQ(Utils::Seal::unseal({link->sealed, link->nonce}, kKey), "gho_secret");
    const std::string raw = Database::get().execute_read(
        [](auto& txn) { return txn.exec("SELECT token_sealed FROM github_links")[0][0].template as<std::string>(); });
    EXPECT_EQ(raw.find("gho_secret"), std::string::npos) << "never in the clear";
    EXPECT_EQ(queued(), 1) << "the first sync is queued";
    const long audited = Database::get().execute_read([](auto& txn) {
        return txn.exec("SELECT count(*) FROM audit_log WHERE action = 'github.link'")[0][0].template as<long>();
    });
    EXPECT_EQ(audited, 1);
}

TEST_F(GithubApiTest, CallbackWithARefusedCodeIsAnError) {
    github.refuse_code = true;
    EXPECT_EQ(location(callback(kAnna, links.new_state(kAnna), "the-code")), "/account?github=error");
    EXPECT_FALSE(links.get(kAnna).has_value());
    EXPECT_EQ(queued(), 0);
}

TEST_F(GithubApiTest, SyncNowQueuesOncePerMinute) {
    const auto anna = principal(kAnna);
    auto resp = sync(anna);
    ASSERT_EQ(resp->statusCode(), k404NotFound);
    EXPECT_EQ(body_of(resp)["error"], "github_not_linked");

    linked(kAnna);
    resp = sync(anna);
    ASSERT_EQ(resp->statusCode(), k202Accepted) << resp->body();
    TestHelpers::expect_matches_schema(resp, "POST", "/api/v1/github/sync");
    EXPECT_EQ(body_of(resp)["data"]["status"], "queued");
    EXPECT_EQ(queued(), 1);

    resp = sync(anna);
    ASSERT_EQ(resp->statusCode(), k429TooManyRequests) << resp->body();
    EXPECT_EQ(body_of(resp)["error"], "sync_too_soon");
    EXPECT_GE(body_of(resp)["retry_after_sec"].get<int>(), 1);
    EXPECT_LE(body_of(resp)["retry_after_sec"].get<int>(), Github::kSyncMinSeconds);
    EXPECT_EQ(queued(), 1) << "not queued twice";

    // A sync that just finished counts as well as a queued one.
    Cache::get().del(std::string("github:sync:") + kAnna);
    links.mark_synced(kAnna, "");
    resp = sync(anna);
    ASSERT_EQ(resp->statusCode(), k429TooManyRequests) << resp->body();
    Database::get().execute_write([](auto& txn) {
        txn.exec("UPDATE github_links SET synced_at = now() - interval '2 minutes'");
        return true;
    });
    EXPECT_EQ(sync(anna)->statusCode(), k202Accepted);
}

TEST_F(GithubApiTest, DisconnectRemovesLinkReposAndMutes) {
    const auto anna = principal(kAnna);
    EXPECT_EQ(disconnect(anna)->statusCode(), k404NotFound);
    linked(kAnna);
    Repositories::Github::RepoRepository::Input in;
    in.node_id = "R_1";
    in.full_name = "moveeeax/one";
    in.owner = "moveeeax";
    in.name = "one";
    Repositories::Github::RepoRepository().replace(kAnna, {in});
    Repositories::Github::MuteRepository().put(kAnna, "moveeeax/one", "issues");
    const auto resp = disconnect(anna);
    ASSERT_EQ(resp->statusCode(), k204NoContent) << resp->body();
    EXPECT_EQ(github.revokes, 1) << "the token is revoked at GitHub";
    EXPECT_FALSE(links.get(kAnna).has_value());
    EXPECT_EQ(Repositories::Github::RepoRepository().list(kAnna, {"all"})["total"], 0);
    EXPECT_TRUE(Repositories::Github::MuteRepository().all(kAnna).empty());
    EXPECT_FALSE(body_of(status(anna))["data"]["linked"].get<bool>());
}
