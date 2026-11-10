/**
 * @file Rules.hpp
 * @brief What needs attention in a repository: six rules over the facts the
 *        sync stores (github_repos), each silenced per repository by a mute
 *        ("Not about this"). Pure: the sync job computes the verdict when it
 *        stores a repository, and a mute put or removed recomputes it from
 *        the stored facts (RepoRepository::facts). An archived repository
 *        needs nothing.
 *
 *        The rules, in the order the verdict lists them:
 *          1. checks   failing checks on the default branch
 *          2. alerts   open Dependabot alerts
 *          3. prs      open pull requests
 *          4. issues   open issues
 *          5. hygiene  no description, no license or no README (public only)
 *          6. stale    no push for twelve months and not archived: archive it?
 */

#pragma once

#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Github {

inline constexpr std::string_view kRuleChecks = "checks";
inline constexpr std::string_view kRuleAlerts = "alerts";
inline constexpr std::string_view kRulePrs = "prs";
inline constexpr std::string_view kRuleIssues = "issues";
inline constexpr std::string_view kRuleHygiene = "hygiene";
inline constexpr std::string_view kRuleStale = "stale";

/// Every rule id, in the order of the verdict (the enum of the API and of github_mutes.rule).
inline constexpr std::array<std::string_view, 6> kRules = {
    kRuleChecks, kRuleAlerts, kRulePrs, kRuleIssues, kRuleHygiene, kRuleStale};

/// A push older than this is stale.
inline constexpr long long kStaleSeconds = 365LL * 86400;

inline bool is_rule(std::string_view rule) {
    for (const auto r : kRules) {
        if (r == rule) {
            return true;
        }
    }
    return false;
}

/// The facts of one repository the rules read (a row of github_repos).
struct RepoFacts {
    std::string full_name;
    bool is_public = true;
    bool fork = false;
    bool archived = false;
    std::string description;
    /// The SPDX id, "" when the repository has no license.
    std::string license;
    bool has_readme = false;
    int open_issues = 0;
    int open_prs = 0;
    int open_alerts = 0;
    /// The default branch head's rollup: success, failure, pending or none.
    std::string checks = "none";
    std::string default_branch;
    /// Unix seconds of the last push; nullopt when never pushed.
    std::optional<long long> pushed_at;
};

namespace detail {

inline std::string counted(int n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

}  // namespace detail

/**
 * @brief The verdict: a JSON array of {rule, detail, code, ...} in rule order,
 *        without the rules in @p muted. Empty for an archived repository.
 *
 *        detail is the English line; code and the numbers next to it let a
 *        client say it in its own words:
 *          checks   failing_checks  branch (the default branch, null when unknown)
 *          alerts   open_alerts     count
 *          prs      open_prs        count
 *          issues   open_issues     count
 *          hygiene  missing_basics  missing (description, license, readme, in that order)
 *          stale    never_pushed, or no_recent_push with months (whole 30-day months since the push)
 * @param now Unix seconds (the stale rule compares pushed_at to it).
 */
inline nlohmann::json attention(const RepoFacts& f, const std::set<std::string>& muted, long long now) {
    nlohmann::json out = nlohmann::json::array();
    if (f.archived) {
        return out;
    }
    const auto add = [&](std::string_view rule, std::string detail, std::string_view code, nlohmann::json fields) {
        if (muted.count(std::string(rule)) == 0) {
            nlohmann::json item = {
                {"rule", std::string(rule)}, {"detail", std::move(detail)}, {"code", std::string(code)}};
            item.update(fields);
            out.push_back(std::move(item));
        }
    };
    if (f.checks == "failure") {
        add(kRuleChecks,
            f.default_branch.empty() ? std::string("checks fail on the default branch")
                                     : "checks fail on " + f.default_branch,
            "failing_checks",
            {{"branch", f.default_branch.empty() ? nlohmann::json() : nlohmann::json(f.default_branch)}});
    }
    if (f.open_alerts > 0) {
        add(kRuleAlerts,
            detail::counted(f.open_alerts, "open Dependabot alert", "open Dependabot alerts"),
            "open_alerts",
            {{"count", f.open_alerts}});
    }
    if (f.open_prs > 0) {
        add(kRulePrs,
            detail::counted(f.open_prs, "open pull request", "open pull requests"),
            "open_prs",
            {{"count", f.open_prs}});
    }
    if (f.open_issues > 0) {
        add(kRuleIssues,
            detail::counted(f.open_issues, "open issue", "open issues"),
            "open_issues",
            {{"count", f.open_issues}});
    }
    if (f.is_public) {
        std::string line;
        nlohmann::json missing = nlohmann::json::array();
        const auto miss = [&](const char* what, const char* id) {
            if (!line.empty()) {
                line += ", ";
            }
            line += what;
            missing.push_back(id);
        };
        if (f.description.empty()) {
            miss("no description", "description");
        }
        if (f.license.empty()) {
            miss("no license", "license");
        }
        if (!f.has_readme) {
            miss("no README", "readme");
        }
        if (!line.empty()) {
            add(kRuleHygiene, line, "missing_basics", {{"missing", missing}});
        }
    }
    if (!f.pushed_at) {
        add(kRuleStale, "never pushed", "never_pushed", nlohmann::json::object());
    } else if (now - *f.pushed_at >= kStaleSeconds) {
        const long long months = (now - *f.pushed_at) / (30LL * 86400);
        add(kRuleStale, "no push for " + std::to_string(months) + " months", "no_recent_push", {{"months", months}});
    }
    return out;
}

}  // namespace Github
