/**
 * @file test_github_rules.cpp
 * @brief Github::attention: the six rules over a repository's facts, the
 *        mutes, the archived short-circuit; Github::is_rule.
 */

#include <set>
#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "github/Rules.hpp"

namespace {

constexpr long long kNow = 1'800'000'000LL;  // 2027-01-15, well after any fact below

Github::RepoFacts tidy() {
    Github::RepoFacts f;
    f.full_name = "moveeeax/life-os-cpp";
    f.is_public = true;
    f.description = "Life OS";
    f.license = "MIT";
    f.has_readme = true;
    f.checks = "success";
    f.default_branch = "master";
    f.pushed_at = kNow - 86400;
    return f;
}

std::set<std::string> rules_of(const nlohmann::json& verdict) {
    std::set<std::string> out;
    for (const auto& item : verdict) {
        out.insert(item.at("rule").get<std::string>());
    }
    return out;
}

}  // namespace

TEST(GithubRules, TidyRepositoryNeedsNothing) {
    const auto v = Github::attention(tidy(), {}, kNow);
    EXPECT_TRUE(v.is_array());
    EXPECT_TRUE(v.empty());
}

TEST(GithubRules, EveryRuleInOrder) {
    Github::RepoFacts f;
    f.full_name = "moveeeax/old";
    f.is_public = true;
    f.checks = "failure";
    f.default_branch = "main";
    f.open_alerts = 2;
    f.open_prs = 1;
    f.open_issues = 3;
    f.pushed_at = kNow - 400LL * 86400;
    const auto v = Github::attention(f, {}, kNow);
    ASSERT_EQ(v.size(), 6u);
    EXPECT_EQ(v[0]["rule"], "checks");
    EXPECT_EQ(v[0]["detail"], "checks fail on main");
    EXPECT_EQ(v[1]["rule"], "alerts");
    EXPECT_EQ(v[1]["detail"], "2 open Dependabot alerts");
    EXPECT_EQ(v[2]["rule"], "prs");
    EXPECT_EQ(v[2]["detail"], "1 open pull request");
    EXPECT_EQ(v[3]["rule"], "issues");
    EXPECT_EQ(v[3]["detail"], "3 open issues");
    EXPECT_EQ(v[4]["rule"], "hygiene");
    EXPECT_EQ(v[4]["detail"], "no description, no license, no README");
    EXPECT_EQ(v[5]["rule"], "stale");
    EXPECT_EQ(v[5]["detail"], "no push for 13 months");
}

TEST(GithubRules, PendingOrNoChecksAreNotFailures) {
    auto f = tidy();
    f.checks = "pending";
    EXPECT_TRUE(Github::attention(f, {}, kNow).empty());
    f.checks = "none";
    EXPECT_TRUE(Github::attention(f, {}, kNow).empty());
    f.checks = "failure";
    f.default_branch.clear();
    const auto v = Github::attention(f, {}, kNow);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0]["detail"], "checks fail on the default branch");
}

TEST(GithubRules, HygieneIsPublicOnlyAndNamesWhatIsMissing) {
    auto f = tidy();
    f.license.clear();
    EXPECT_EQ(rules_of(Github::attention(f, {}, kNow)), std::set<std::string>{"hygiene"});
    EXPECT_EQ(Github::attention(f, {}, kNow)[0]["detail"], "no license");
    f.is_public = false;
    EXPECT_TRUE(Github::attention(f, {}, kNow).empty());
}

TEST(GithubRules, StaleIsTwelveMonthsAndNeverPushed) {
    auto f = tidy();
    f.pushed_at = kNow - 364LL * 86400;
    EXPECT_TRUE(Github::attention(f, {}, kNow).empty());
    f.pushed_at = kNow - 365LL * 86400;
    auto v = Github::attention(f, {}, kNow);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0]["rule"], "stale");
    EXPECT_EQ(v[0]["detail"], "no push for 12 months");
    f.pushed_at.reset();
    v = Github::attention(f, {}, kNow);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0]["detail"], "never pushed");
}

TEST(GithubRules, ArchivedNeedsNothing) {
    Github::RepoFacts f;
    f.archived = true;
    f.checks = "failure";
    f.open_issues = 9;
    f.pushed_at.reset();
    EXPECT_TRUE(Github::attention(f, {}, kNow).empty());
}

TEST(GithubRules, MutesDropTheirRulesOnly) {
    auto f = tidy();
    f.open_prs = 2;
    f.open_issues = 1;
    const auto v = Github::attention(f, {"prs", "stale"}, kNow);
    EXPECT_EQ(rules_of(v), std::set<std::string>{"issues"});
}

TEST(GithubRules, RuleIds) {
    for (const auto r : Github::kRules) {
        EXPECT_TRUE(Github::is_rule(r));
    }
    EXPECT_EQ(Github::kRules.size(), 6u);
    EXPECT_EQ(Github::kRules[0], "checks");
    EXPECT_EQ(Github::kRules[5], "stale");
    EXPECT_FALSE(Github::is_rule(""));
    EXPECT_FALSE(Github::is_rule("Checks"));
    EXPECT_FALSE(Github::is_rule("readme"));
}
