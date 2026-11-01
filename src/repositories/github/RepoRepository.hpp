/**
 * @file RepoRepository.hpp
 * @brief github_repos (migration 079): the repositories of a user as the last
 *        sync saw them, keyed by GitHub's node id, with the facts the
 *        attention rules read (github/Rules.hpp), their verdict (attention)
 *        and the tasks made from it (task_ids, rule -> task id).
 *
 *        A row of the API (GithubRepo of docs/openapi.yaml): full_name, owner,
 *        name, visibility, fork, archived, description, topics, license,
 *        stars, pushed_at, default_branch, open_issues, open_prs, open_alerts,
 *        checks, has_readme, attention, muted, tasks, synced_at.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "github/Rules.hpp"
#include "repositories/LikePattern.hpp"
#include "repositories/SqlTime.hpp"

namespace Repositories::Github {

class RepoRepository {
public:
    /// One repository as the sync hands it over (attention already computed by Github::attention).
    struct Input {
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
        std::optional<std::string> pushed_at;  // ISO 8601 (GitHub's pushedAt), nullopt when never pushed
        std::string default_branch;
        int open_issues = 0;
        int open_prs = 0;
        int open_alerts = 0;
        std::string checks = "none";  // success | failure | pending | none
        bool has_readme = false;
        nlohmann::json attention = nlohmann::json::array();
    };

    struct Filter {
        std::string view = "attention";  // attention | all | archived
        std::string owner;               // "" any, or the repository owner (user or org login)
        std::string q;                   // over the full name and the topics
        int limit = 50;
        int offset = 0;
    };

    /// The repositories of @p owner become @p repos in one transaction: rows whose node id is not in the list go,
    /// the others are inserted or updated (task_ids kept), all with synced_at = now.
    void replace(const std::string& owner, const std::vector<Input>& repos) {
        Database::get().execute_write([&](auto& txn) {
            nlohmann::json ids = nlohmann::json::array();
            for (const auto& r : repos) {
                ids.push_back(r.node_id);
            }
            txn.exec_params(
                "DELETE FROM github_repos WHERE owner_id = $1::uuid AND NOT (gh_node_id = ANY(ARRAY(SELECT "
                "jsonb_array_elements_text($2::jsonb))))",
                owner,
                ids.dump());
            for (const auto& r : repos) {
                txn.exec_params(
                    "INSERT INTO github_repos (owner_id, gh_node_id, full_name, repo_owner, name, visibility, fork, "
                    "archived, description, topics, license, stars, pushed_at, default_branch, open_issues, open_prs, "
                    "open_alerts, checks, has_readme, attention, synced_at) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, "
                    "$8, $9, ARRAY(SELECT jsonb_array_elements_text($10::jsonb)), $11, $12, NULLIF($13, "
                    "'')::timestamptz, $14, $15, $16, $17, $18, $19, $20::jsonb, now()) ON CONFLICT (owner_id, "
                    "gh_node_id) DO UPDATE SET full_name = EXCLUDED.full_name, repo_owner = EXCLUDED.repo_owner, "
                    "name = EXCLUDED.name, visibility = EXCLUDED.visibility, fork = EXCLUDED.fork, archived = "
                    "EXCLUDED.archived, description = EXCLUDED.description, topics = EXCLUDED.topics, license = "
                    "EXCLUDED.license, stars = EXCLUDED.stars, pushed_at = EXCLUDED.pushed_at, default_branch = "
                    "EXCLUDED.default_branch, open_issues = EXCLUDED.open_issues, open_prs = EXCLUDED.open_prs, "
                    "open_alerts = EXCLUDED.open_alerts, checks = EXCLUDED.checks, has_readme = EXCLUDED.has_readme, "
                    "attention = EXCLUDED.attention, synced_at = now()",
                    owner,
                    r.node_id,
                    r.full_name,
                    r.owner,
                    r.name,
                    r.visibility,
                    r.fork,
                    r.archived,
                    r.description,
                    nlohmann::json(r.topics).dump(),
                    r.license,
                    r.stars,
                    r.pushed_at.value_or(""),
                    r.default_branch,
                    r.open_issues,
                    r.open_prs,
                    r.open_alerts,
                    r.checks,
                    r.has_readme,
                    r.attention.dump());
            }
            return true;
        });
    }

    /// {data: rows of the view, total, owners: the distinct repository owners, counts: {attention, all, archived}}.
    /// attention: not archived and with a verdict; all: not archived; archived: archived. Last push first.
    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            const std::string where =
                " WHERE r.owner_id = $1::uuid AND (($2 = 'all' AND NOT r.archived) OR ($2 = 'archived' AND "
                "r.archived) OR ($2 = 'attention' AND NOT r.archived AND jsonb_array_length(r.attention) > 0)) AND "
                "($3 = '' OR r.repo_owner = $3) AND ($4 = '' OR r.full_name ILIKE '%' || $4 || '%' ESCAPE '\\' OR "
                "EXISTS (SELECT 1 FROM unnest(r.topics) t WHERE t ILIKE '%' || $4 || '%' ESCAPE '\\')) ";
            const std::string q = Repositories::escape_like(f.q);
            auto total =
                txn.exec_params("SELECT count(*) FROM github_repos r" + where + " AND $5::int >= 0 AND $6::int >= 0",
                                owner,
                                f.view,
                                f.owner,
                                q,
                                f.limit,
                                f.offset);
            auto rows = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json)::text FROM (SELECT " +
                                            std::string(kRowColumns) + " FROM github_repos r" + where +
                                            " ORDER BY r.pushed_at DESC NULLS LAST, r.full_name LIMIT $5 OFFSET $6) t",
                                        owner,
                                        f.view,
                                        f.owner,
                                        q,
                                        f.limit,
                                        f.offset);
            auto owners = txn.exec_params(
                "SELECT COALESCE(json_agg(o), '[]'::json)::text FROM (SELECT DISTINCT repo_owner AS o FROM "
                "github_repos WHERE owner_id = $1::uuid ORDER BY 1) s",
                owner);
            auto counts = txn.exec_params(
                "SELECT json_build_object('attention', count(*) FILTER (WHERE NOT archived AND "
                "jsonb_array_length(attention) > 0), 'all', count(*) FILTER (WHERE NOT archived), 'archived', "
                "count(*) FILTER (WHERE archived))::text FROM github_repos WHERE owner_id = $1::uuid",
                owner);
            return nlohmann::json{{"data", nlohmann::json::parse(rows[0][0].template as<std::string>())},
                                  {"total", total[0][0].template as<long>()},
                                  {"owners", nlohmann::json::parse(owners[0][0].template as<std::string>())},
                                  {"counts", nlohmann::json::parse(counts[0][0].template as<std::string>())}};
        });
    }

    /// One row by its full name ("owner/name"), the freshest when a rename left two; nullopt when unknown.
    std::optional<nlohmann::json> one(const std::string& owner, const std::string& full_name) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params("SELECT row_to_json(t)::text FROM (SELECT " + std::string(kRowColumns) +
                                         " FROM github_repos r WHERE r.owner_id = $1::uuid AND r.full_name = $2 "
                                         "ORDER BY r.synced_at DESC LIMIT 1) t",
                                     owner,
                                     full_name);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The facts of one repository for Github::attention (a mute changed); nullopt when unknown.
    std::optional<::Github::RepoFacts> facts(const std::string& owner, const std::string& full_name) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<::Github::RepoFacts> {
            auto r = txn.exec_params(
                "SELECT full_name, visibility = 'public', fork, archived, description, license, has_readme, "
                "open_issues, open_prs, open_alerts, checks, default_branch, extract(epoch FROM pushed_at)::bigint "
                "FROM github_repos WHERE owner_id = $1::uuid AND full_name = $2 ORDER BY synced_at DESC LIMIT 1",
                owner,
                full_name);
            if (r.empty()) {
                return std::nullopt;
            }
            ::Github::RepoFacts f;
            f.full_name = r[0][0].template as<std::string>();
            f.is_public = r[0][1].template as<bool>();
            f.fork = r[0][2].template as<bool>();
            f.archived = r[0][3].template as<bool>();
            f.description = r[0][4].template as<std::string>();
            f.license = r[0][5].template as<std::string>();
            f.has_readme = r[0][6].template as<bool>();
            f.open_issues = r[0][7].template as<int>();
            f.open_prs = r[0][8].template as<int>();
            f.open_alerts = r[0][9].template as<int>();
            f.checks = r[0][10].template as<std::string>();
            f.default_branch = r[0][11].template as<std::string>();
            if (!r[0][12].is_null()) {
                f.pushed_at = r[0][12].template as<long long>();
            }
            return f;
        });
    }

    /// Stores a recomputed verdict; false when the repository is unknown.
    bool set_attention(const std::string& owner, const std::string& full_name, const nlohmann::json& attention) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE github_repos SET attention = $3::jsonb WHERE owner_id = $1::uuid AND full_name = $2",
                          owner,
                          full_name,
                          attention.dump())
                       .affected_rows() > 0;
        });
    }

    /// The task made from @p rule of this repository, if any ("Make a task" opens it on a second tap).
    std::optional<std::string> task_of(const std::string& owner,
                                       const std::string& full_name,
                                       const std::string& rule) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "SELECT task_ids ->> $3 FROM github_repos WHERE owner_id = $1::uuid AND full_name = $2 ORDER BY "
                "synced_at DESC LIMIT 1",
                owner,
                full_name,
                rule);
            if (r.empty() || r[0][0].is_null()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    /// Remembers the task made from @p rule; false when the repository is unknown.
    bool set_task(const std::string& owner,
                  const std::string& full_name,
                  const std::string& rule,
                  const std::string& task_id) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE github_repos SET task_ids = task_ids || jsonb_build_object($3::text, $4::text) WHERE "
                          "owner_id = $1::uuid AND full_name = $2",
                          owner,
                          full_name,
                          rule,
                          task_id)
                       .affected_rows() > 0;
        });
    }

    /// Forgets the task of @p rule (it was deleted; the next tap makes a new one).
    void clear_task(const std::string& owner, const std::string& full_name, const std::string& rule) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE github_repos SET task_ids = task_ids - $3::text WHERE owner_id = $1::uuid AND full_name = $2",
                owner,
                full_name,
                rule);
            return true;
        });
    }

    /// How many repositories need attention, and the full names of the first @p top by last push (the Sunday line).
    nlohmann::json attention_summary(const std::string& owner, int top) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT json_build_object('count', (SELECT count(*) FROM github_repos WHERE owner_id = $1::uuid AND "
                "NOT archived AND jsonb_array_length(attention) > 0), 'top', COALESCE((SELECT json_agg(full_name) "
                "FROM (SELECT full_name FROM github_repos WHERE owner_id = $1::uuid AND NOT archived AND "
                "jsonb_array_length(attention) > 0 ORDER BY pushed_at DESC NULLS LAST, full_name LIMIT $2) s), "
                "'[]'::json))::text",
                owner,
                top);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

private:
    static constexpr const char* kRowColumns =
        "r.full_name, r.repo_owner AS owner, r.name, r.visibility, r.fork, r.archived, r.description, r.topics, "
        "r.license, r.stars, " SQL_ISO_UTC("r.pushed_at") " AS pushed_at, r.default_branch, r.open_issues, "
        "r.open_prs, r.open_alerts, r.checks, r.has_readme, r.attention, COALESCE((SELECT array_agg(m.rule ORDER BY "
        "m.rule) FROM github_mutes m WHERE m.owner_id = r.owner_id AND m.full_name = r.full_name), '{}'::text[]) AS "
        "muted, r.task_ids AS tasks, " SQL_ISO_UTC("r.synced_at") " AS synced_at";
};

}  // namespace Repositories::Github
