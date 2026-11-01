/**
 * @file Client.hpp
 * @brief GitHub as Life OS reaches it: the OAuth App's authorize URL, the
 *        code exchange, the revoke, and one GraphQL pull of every repository
 *        the viewer owns plus those of each organisation they belong to
 *        (100 a page, by cursor). Pure over Net::Http::Transport, so a test
 *        scripts GitHub; the pure parts (the mapping of a node, the facts
 *        of a repository) carry the unit tests. No database here: the sync
 *        job (github/jobs/Sync.hpp) stores what pull() gives.
 *
 *        A failure names itself with a code the Account card can show as
 *        sync_error: token_revoked (GitHub answered 401: connect again),
 *        rate_limited (403/429 or a RATE_LIMITED error: later), forbidden
 *        (403 of another kind), unavailable (no answer or a 5xx) and
 *        bad_answer (an answer that is not what the query asked).
 */

#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "github/Rules.hpp"
#include "github/Settings.hpp"
#include "net/Http.hpp"

namespace Github {

/// GitHub refuses a request without a User-Agent; this names the site.
inline constexpr const char* kUserAgent = "Life OS (https://life-os.tarassov.me)";
inline constexpr const char* kApiVersion = "2022-11-28";
/// Repositories a page asks for, and the pages one owner may take (5000 repositories).
inline constexpr int kPageSize = 100;
inline constexpr int kMaxPages = 50;

/// @p s percent-encoded for a query value or a form field.
std::string encode(const std::string& s);

/// The authorize URL for @p state with the scopes of kScope.
std::string auth_url(const Settings& s, const std::string& state);

/// What the code exchange gave: the token and the scopes GitHub granted ("repo read:org"), or the error it named.
struct Tokens {
    std::string access_token;
    std::string scopes;
    std::string error;
    bool ok() const { return error.empty() && !access_token.empty(); }
};

Tokens exchange(Net::Http::Transport& t, const Settings& s, const std::string& code);

/// Best effort: false when GitHub did not take it (DELETE /applications/{client_id}/token).
bool revoke(Net::Http::Transport& t, const Settings& s, const std::string& token);

/// Why a call failed: one of the codes of the file comment, and words for the log.
struct Failure {
    std::string code;
    std::string why;
};

/// The login of the token's user; a Failure when GitHub refused or did not answer.
std::variant<std::string, Failure> viewer_login(Net::Http::Transport& t, const std::string& token);

/// One repository as GitHub answered it (the fields of github_repos the sync stores).
struct Repo {
    std::string node_id;
    std::string full_name;              // "moveeeax/life-os-cpp"
    std::string owner;                  // "moveeeax"
    std::string name;                   // "life-os-cpp"
    std::string visibility = "public";  // public | private | internal
    bool fork = false;
    bool archived = false;
    std::string description;
    std::vector<std::string> topics;
    std::string license;  // SPDX id, "" when none
    int stars = 0;
    std::optional<std::string> pushed_at;  // GitHub's pushedAt (ISO 8601), nullopt when never pushed
    std::string default_branch;
    int open_issues = 0;
    int open_prs = 0;
    int open_alerts = 0;
    std::string checks = "none";  // success | failure | pending | none
    bool has_readme = false;
};

/// Everything one pull gave.
struct Pull {
    std::string login;
    std::vector<Repo> repos;
    /// The organisations the viewer belongs to, and those of them whose repositories came back empty or
    /// forbidden (the organisation restricts OAuth apps until the owner approves this one).
    std::vector<std::string> orgs;
    std::vector<std::string> empty_orgs;
};

/// The viewer's own repositories and those of each of their organisations, every page.
std::variant<Pull, Failure> pull(Net::Http::Transport& t, const std::string& token);

/// The Repo of one GraphQL node of the query's fragment; nullopt for a node without an id or a name.
std::optional<Repo> repo_of(const nlohmann::json& node);

/// The facts the attention rules read (github/Rules.hpp).
RepoFacts facts_of(const Repo& r);

/// GitHub's StatusState (SUCCESS, FAILURE, ERROR, PENDING, EXPECTED) as the checks column keeps it.
std::string checks_of(const std::string& state);

/// GitHub's DateTime ("2026-10-29T11:00:00Z") to unix seconds; nullopt when it is not one.
std::optional<long long> parse_time(const std::string& s);

/// One GraphQL call: the `data` object, or the Failure the status or the errors name.
std::variant<nlohmann::json, Failure> graphql(Net::Http::Transport& t,
                                              const std::string& token,
                                              const std::string& query,
                                              const nlohmann::json& variables);

}  // namespace Github
