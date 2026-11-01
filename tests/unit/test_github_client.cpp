/**
 * @file test_github_client.cpp
 * @brief GitHub's client over a scripted transport (github/Client.hpp): the
 *        authorize URL, the code exchange, the mapping of a GraphQL node to
 *        a repository and its facts, a pull over pages and organisations,
 *        and the codes a refusal gets (401, rate limits, no answer).
 */

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "github/Client.hpp"
#include "net/Http.hpp"

namespace {

using nlohmann::json;

const Github::Settings kSettings{"cid123", "sec", "https://life.test/api/v1/github/oauth/callback", "key"};

json repo_node(const std::string& id, const std::string& owner, const std::string& name) {
    return json{{"id", id},
                {"name", name},
                {"nameWithOwner", owner + "/" + name},
                {"owner", {{"login", owner}}},
                {"visibility", "PUBLIC"},
                {"isFork", false},
                {"isArchived", false},
                {"description", "A thing"},
                {"repositoryTopics", {{"nodes", json::array({json{{"topic", {{"name", "cpp"}}}}})}}},
                {"licenseInfo", {{"spdxId", "MIT"}}},
                {"stargazerCount", 3},
                {"pushedAt", "2026-10-29T11:00:00Z"},
                {"defaultBranchRef", {{"name", "master"}, {"target", {{"statusCheckRollup", {{"state", "SUCCESS"}}}}}}},
                {"readmeMd", {{"id", "blob1"}}},
                {"readmePlain", nullptr},
                {"readmeRst", nullptr},
                {"issues", {{"totalCount", 2}}},
                {"pullRequests", {{"totalCount", 1}}},
                {"vulnerabilityAlerts", {{"totalCount", 0}}}};
}

json page(const std::vector<json>& nodes, bool more, const std::string& cursor) {
    return json{{"pageInfo", {{"hasNextPage", more}, {"endCursor", cursor}}}, {"nodes", nodes}};
}

/// GitHub as scripted: the viewer owns two pages, cybercapybara answers, locked is forbidden.
struct ScriptedGitHub : Net::Http::Transport {
    long status = 200;
    std::string body_override;
    bool unreachable = false;
    std::vector<json> queries;
    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        return {404, ""};
    }
    Net::Http::Response post_json(const std::string& url,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>& headers,
                                  long) override {
        if (unreachable) {
            throw Net::Http::TransportError("dns");
        }
        bool agent = false;
        for (const auto& [k, v] : headers) {
            agent = agent || (k == "User-Agent" && !v.empty());
        }
        EXPECT_TRUE(agent) << "GitHub refuses a call without a User-Agent";
        EXPECT_EQ(url, std::string(Github::kGraphqlUrl));
        const json q = json::parse(body);
        queries.push_back(q);
        if (!body_override.empty() || status != 200) {
            return {status, body_override};
        }
        const std::string query = q.value("query", "");
        const json vars = q.value("variables", json::object());
        if (query.find("organization(login:") != std::string::npos) {
            const std::string login = vars.value("login", "");
            if (login == "locked") {
                return {200,
                        json{{"data", {{"organization", nullptr}}},
                             {"errors", json::array({json{{"type", "FORBIDDEN"}, {"message", "restricted"}}})}}
                            .dump()};
            }
            return {
                200,
                json{{"data", {{"organization", {{"repositories", page({repo_node("o1", login, "bot")}, false, "")}}}}}}
                    .dump()};
        }
        if (query.find("viewer {") != std::string::npos && query.find("repositories(") == std::string::npos) {
            return {200, json{{"data", {{"viewer", {{"login", "moveeeax"}}}}}}.dump()};
        }
        const bool second = vars.contains("after") && vars["after"].is_string();
        json viewer{
            {"login", "moveeeax"},
            {"organizations", {{"nodes", json::array({json{{"login", "cybercapybara"}}, json{{"login", "locked"}}})}}},
            {"repositories",
             second ? page({repo_node("r2", "moveeeax", "second")}, false, "")
                    : page({repo_node("r1", "moveeeax", "first")}, true, "c1")}};
        return {200, json{{"data", {{"viewer", viewer}}}}.dump()};
    }
    Net::Http::Response send(const Net::Http::Request& r) override {
        if (unreachable) {
            throw Net::Http::TransportError("dns");
        }
        if (r.url == std::string(Github::kTokenUrl)) {
            EXPECT_EQ(r.method, "POST");
            EXPECT_NE(r.body.find("code=the-code"), std::string::npos);
            EXPECT_NE(r.body.find("client_secret=sec"), std::string::npos);
            if (!body_override.empty()) {
                return {status, body_override};
            }
            return {200, R"({"access_token":"gho_abc","scope":"repo,read:org","token_type":"bearer"})"};
        }
        if (r.url.find("/applications/cid123/token") != std::string::npos) {
            EXPECT_EQ(r.method, "DELETE");
            bool basic = false;
            for (const auto& [k, v] : r.headers) {
                basic = basic || (k == "Authorization" && v == "Basic Y2lkMTIzOnNlYw==");
            }
            EXPECT_TRUE(basic) << "the revoke call carries the App's credentials";
            return {204, ""};
        }
        return {404, ""};
    }
};

}  // namespace

TEST(GithubClient, AuthUrlCarriesClientRedirectScopeAndState) {
    const std::string url = Github::auth_url(kSettings, "st4te");
    EXPECT_EQ(url.rfind("https://github.com/login/oauth/authorize?", 0), 0u);
    EXPECT_NE(url.find("client_id=cid123"), std::string::npos);
    EXPECT_NE(url.find("redirect_uri=https%3A%2F%2Flife.test%2Fapi%2Fv1%2Fgithub%2Foauth%2Fcallback"),
              std::string::npos);
    EXPECT_NE(url.find("scope=repo%20read%3Aorg"), std::string::npos);
    EXPECT_NE(url.find("state=st4te"), std::string::npos);
    EXPECT_EQ(url.find("sec"), std::string::npos) << "the secret never goes to the browser";
}

TEST(GithubClient, ExchangeGivesTokenAndNormalisedScopes) {
    ScriptedGitHub gh;
    const auto t = Github::exchange(gh, kSettings, "the-code");
    ASSERT_TRUE(t.ok()) << t.error;
    EXPECT_EQ(t.access_token, "gho_abc");
    EXPECT_EQ(t.scopes, "repo read:org");
}

TEST(GithubClient, ExchangeNamesGithubsErrorEvenOn200) {
    ScriptedGitHub gh;
    gh.body_override = R"({"error":"bad_verification_code","error_description":"The code passed is incorrect."})";
    const auto t = Github::exchange(gh, kSettings, "the-code");
    EXPECT_FALSE(t.ok());
    EXPECT_EQ(t.error, "bad_verification_code");
    gh.body_override.clear();
    gh.unreachable = true;
    EXPECT_EQ(Github::exchange(gh, kSettings, "the-code").error, "unavailable");
}

TEST(GithubClient, RevokeCarriesBasicCredentials) {
    ScriptedGitHub gh;
    EXPECT_TRUE(Github::revoke(gh, kSettings, "gho_abc"));
    gh.unreachable = true;
    EXPECT_FALSE(Github::revoke(gh, kSettings, "gho_abc"));
}

TEST(GithubClient, RepoOfMapsTheFragment) {
    const auto r = Github::repo_of(repo_node("R_1", "moveeeax", "life-os-cpp"));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->node_id, "R_1");
    EXPECT_EQ(r->full_name, "moveeeax/life-os-cpp");
    EXPECT_EQ(r->owner, "moveeeax");
    EXPECT_EQ(r->name, "life-os-cpp");
    EXPECT_EQ(r->visibility, "public");
    EXPECT_EQ(r->description, "A thing");
    ASSERT_EQ(r->topics.size(), 1u);
    EXPECT_EQ(r->topics[0], "cpp");
    EXPECT_EQ(r->license, "MIT");
    EXPECT_EQ(r->stars, 3);
    EXPECT_EQ(r->pushed_at, "2026-10-29T11:00:00Z");
    EXPECT_EQ(r->default_branch, "master");
    EXPECT_EQ(r->checks, "success");
    EXPECT_TRUE(r->has_readme);
    EXPECT_EQ(r->open_issues, 2);
    EXPECT_EQ(r->open_prs, 1);
    EXPECT_EQ(r->open_alerts, 0);
    const auto f = Github::facts_of(*r);
    EXPECT_TRUE(f.is_public);
    ASSERT_TRUE(f.pushed_at.has_value());
    EXPECT_EQ(*f.pushed_at, 1793271600) << "2026-10-29T11:00:00Z";
}

TEST(GithubClient, RepoOfTakesNullsAsAbsent) {
    json n = repo_node("R_2", "moveeeax", "bare");
    n["visibility"] = "PRIVATE";
    n["description"] = nullptr;
    n["licenseInfo"] = nullptr;
    n["pushedAt"] = nullptr;
    n["defaultBranchRef"] = nullptr;
    n["readmeMd"] = nullptr;
    n["readmeRst"] = {{"id", "blob2"}};
    n["repositoryTopics"] = {{"nodes", json::array()}};
    n["vulnerabilityAlerts"] = nullptr;
    const auto r = Github::repo_of(n);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->visibility, "private");
    EXPECT_EQ(r->description, "");
    EXPECT_EQ(r->license, "");
    EXPECT_FALSE(r->pushed_at.has_value());
    EXPECT_EQ(r->default_branch, "");
    EXPECT_EQ(r->checks, "none");
    EXPECT_TRUE(r->has_readme) << "README.rst counts";
    EXPECT_EQ(r->open_alerts, 0) << "alerts the token may not see count as none";
    EXPECT_FALSE(Github::facts_of(*r).pushed_at.has_value());
    EXPECT_FALSE(Github::repo_of(json{{"name", "no-id"}}).has_value());
    EXPECT_FALSE(Github::repo_of(nullptr).has_value());
}

TEST(GithubClient, ChecksStateMapping) {
    EXPECT_EQ(Github::checks_of("SUCCESS"), "success");
    EXPECT_EQ(Github::checks_of("FAILURE"), "failure");
    EXPECT_EQ(Github::checks_of("ERROR"), "failure");
    EXPECT_EQ(Github::checks_of("PENDING"), "pending");
    EXPECT_EQ(Github::checks_of("EXPECTED"), "pending");
    EXPECT_EQ(Github::checks_of(""), "none");
}

TEST(GithubClient, ParsesGithubTimes) {
    EXPECT_EQ(Github::parse_time("1970-01-01T00:00:00Z"), 0);
    EXPECT_EQ(Github::parse_time("2026-10-29T11:00:00+05:00"), Github::parse_time("2026-10-29T06:00:00Z"));
    EXPECT_FALSE(Github::parse_time("2026-10-29").has_value());
    EXPECT_FALSE(Github::parse_time("2026-10-29T11:00:00").has_value());
}

TEST(GithubClient, PullWalksPagesAndOrganisations) {
    ScriptedGitHub gh;
    auto out = Github::pull(gh, "gho_abc");
    ASSERT_TRUE(std::holds_alternative<Github::Pull>(out)) << std::get<Github::Failure>(out).why;
    const auto& p = std::get<Github::Pull>(out);
    EXPECT_EQ(p.login, "moveeeax");
    ASSERT_EQ(p.repos.size(), 3u) << "two pages of the viewer plus cybercapybara";
    EXPECT_EQ(p.repos[0].full_name, "moveeeax/first");
    EXPECT_EQ(p.repos[1].full_name, "moveeeax/second");
    EXPECT_EQ(p.repos[2].full_name, "cybercapybara/bot");
    ASSERT_EQ(p.orgs.size(), 2u);
    ASSERT_EQ(p.empty_orgs.size(), 1u);
    EXPECT_EQ(p.empty_orgs[0], "locked") << "a forbidden organisation is the hint, not a failure";
    ASSERT_EQ(gh.queries.size(), 4u) << "viewer page 1, page 2, cybercapybara, locked";
    EXPECT_EQ(gh.queries[1]["variables"]["after"], "c1");
    EXPECT_EQ(gh.queries[0]["variables"]["first"], Github::kPageSize);
    EXPECT_NE(gh.queries[0]["query"].get<std::string>().find("affiliations: [OWNER]"), std::string::npos);
}

TEST(GithubClient, ViewerLogin) {
    ScriptedGitHub gh;
    auto login = Github::viewer_login(gh, "gho_abc");
    ASSERT_TRUE(std::holds_alternative<std::string>(login));
    EXPECT_EQ(std::get<std::string>(login), "moveeeax");
}

TEST(GithubClient, RefusalsGetTheirCodes) {
    const auto code = [](ScriptedGitHub& gh) {
        auto out = Github::pull(gh, "gho_abc");
        return std::holds_alternative<Github::Failure>(out) ? std::get<Github::Failure>(out).code : std::string("ok");
    };
    ScriptedGitHub gh;
    gh.status = 401;
    gh.body_override = R"({"message":"Bad credentials"})";
    EXPECT_EQ(code(gh), "token_revoked");
    gh.status = 429;
    EXPECT_EQ(code(gh), "rate_limited");
    gh.status = 403;
    gh.body_override = R"({"message":"You have exceeded a secondary rate limit. Please wait a few minutes."})";
    EXPECT_EQ(code(gh), "rate_limited");
    gh.body_override = R"({"message":"Resource not accessible by integration"})";
    EXPECT_EQ(code(gh), "forbidden");
    gh.status = 502;
    gh.body_override = "bad gateway";
    EXPECT_EQ(code(gh), "unavailable");
    gh.status = 200;
    gh.body_override = json{{"errors", json::array({json{{"type", "RATE_LIMITED"}, {"message", "later"}}})}}.dump();
    EXPECT_EQ(code(gh), "rate_limited");
    gh.body_override = "<html>";
    EXPECT_EQ(code(gh), "bad_answer");
    gh.body_override.clear();
    gh.unreachable = true;
    EXPECT_EQ(code(gh), "unavailable");
}
