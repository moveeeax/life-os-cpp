/**
 * @file NoteRepository.hpp
 * @brief The owner's notes: Markdown with the fields of a tarassov.me post
 *        (slug, title, summary, body, topic, tags, status), linked to a project
 *        or course and to the task a note was made from.
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Notes {

struct NotFound : NotFoundError {
    NotFound() : NotFoundError("note") {}
};

struct SlugTaken : ConflictError {
    SlugTaken() : ConflictError("slug_taken", "another note of yours has this slug") {}
};

struct Invariant : ValidationError {
    Invariant() : ValidationError("invariant", "the row breaks a rule of the notes model") {}
};

class NoteRepository {
public:
    struct Input {
        std::string slug;
        std::string title;
        std::string summary;
        std::string body;
        std::string topic;
        std::vector<std::string> tags;
        std::optional<std::string> project_id;
        std::optional<std::string> task_id;
    };

    /// A partial edit; a link given as null clears it.
    struct Patch {
        std::optional<std::string> slug;
        std::optional<std::string> title;
        std::optional<std::string> summary;
        std::optional<std::string> body;
        std::optional<std::string> topic;
        std::optional<std::vector<std::string>> tags;
        std::optional<std::optional<std::string>> project_id;
        std::optional<std::optional<std::string>> task_id;
    };

    struct Filter {
        std::string q;        // over the title, the summary and the body
        std::string topic;    // "" any
        std::string tag;      // "" any
        std::string project;  // "" any, or a project id
        int limit = 50;
        int offset = 0;
    };

    /// @throws SlugTaken, Invariant.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO notes (owner_id, slug, title, summary, body, topic, tags, project_id, task_id) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7::text[], $8::uuid, $9::uuid) RETURNING id::text",
                        owner,
                        in.slug,
                        in.title,
                        in.summary,
                        in.body,
                        in.topic,
                        pg_array(in.tags),
                        in.project_id,
                        in.task_id);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// The slugs the owner already has.
    std::set<std::string> slugs(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::set<std::string> out;
            for (const auto& row : txn.exec_params("SELECT slug FROM notes WHERE owner_id = $1::uuid", owner)) {
                out.insert(row[0].template as<std::string>());
            }
            return out;
        });
    }

    /// @throws NotFound, SlugTaken, Invariant.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE notes SET slug = COALESCE($3, slug), title = COALESCE($4, title), "
                        " summary = COALESCE($5, summary), body = COALESCE($6, body), topic = COALESCE($7, topic), "
                        " tags = CASE WHEN $8::boolean THEN $9::text[] ELSE tags END, "
                        " project_id = CASE WHEN $10::boolean THEN $11::uuid ELSE project_id END, "
                        " task_id = CASE WHEN $12::boolean THEN $13::uuid ELSE task_id END, updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.slug,
                        p.title,
                        p.summary,
                        p.body,
                        p.topic,
                        p.tags.has_value(),
                        p.tags ? pg_array(*p.tags) : std::string("{}"),
                        p.project_id.has_value(),
                        p.project_id.has_value() ? *p.project_id : std::optional<std::string>(),
                        p.task_id.has_value(),
                        p.task_id.has_value() ? *p.task_id : std::optional<std::string>());
                    if (r.empty()) {
                        throw NotFound();
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &translate);
    }

    /// @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            if (txn.exec_params("DELETE FROM notes WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id)
                    .empty()) {
                throw NotFound();
            }
            return true;
        });
    }

    /// {data: newest first, total}; the body is left out of the list (topics() gives the facets).
    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            const std::string where =
                " WHERE n.owner_id = $1::uuid AND ($2 = '' OR n.title ILIKE '%' || $2 || '%' ESCAPE '\\' "
                " OR n.summary ILIKE '%' || $2 || '%' ESCAPE '\\' OR n.body ILIKE '%' || $2 || '%' ESCAPE '\\') "
                " AND ($3 = '' OR n.topic = $3) AND ($4 = '' OR $4 = ANY(n.tags)) "
                " AND ($5 = '' OR n.project_id = NULLIF($5, '')::uuid) ";
            const std::string q = escape_like(f.q);
            auto total = txn.exec_params("SELECT count(*) FROM notes n" + where + " AND $6::int >= 0 AND $7::int >= 0",
                                         owner,
                                         q,
                                         f.topic,
                                         f.tag,
                                         f.project,
                                         f.limit,
                                         f.offset);
            auto r =
                txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kListColumns) +
                                    " FROM notes n" + where + " ORDER BY n.created_at DESC LIMIT $6 OFFSET $7) t",
                                owner,
                                q,
                                f.topic,
                                f.tag,
                                f.project,
                                f.limit,
                                f.offset);
            return nlohmann::json{{"data", nlohmann::json::parse(r[0][0].template as<std::string>())},
                                  {"total", total[0][0].template as<long>()}};
        });
    }

    /// {topics: [{topic, count}], tags: [{tag, count}]} in use over all the owner's notes, most used first.
    nlohmann::json topics(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto t = txn.exec_params(
                "SELECT COALESCE(json_agg(x), '[]'::json) FROM (SELECT topic, count(*) AS count FROM notes "
                " WHERE owner_id = $1::uuid AND topic <> '' GROUP BY topic ORDER BY count DESC, topic) x",
                owner);
            auto g = txn.exec_params(
                "SELECT COALESCE(json_agg(x), '[]'::json) FROM (SELECT tag, count(*) AS count FROM notes, "
                " unnest(tags) AS tag WHERE owner_id = $1::uuid GROUP BY tag ORDER BY count DESC, tag) x",
                owner);
            return nlohmann::json{{"topics", nlohmann::json::parse(t[0][0].template as<std::string>())},
                                  {"tags", nlohmann::json::parse(g[0][0].template as<std::string>())}};
        });
    }

private:
    static constexpr const char* kTimes =
        " to_char(n.published_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS published_at, "
        " to_char(n.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(n.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";
    /// The project or course of a note, by name and kind, as tasks carry theirs.
    static constexpr const char* kLinks =
        " n.project_id, n.task_id, "
        " (SELECT p.name FROM projects p WHERE p.id = n.project_id) AS project_title, "
        " (SELECT p.kind FROM projects p WHERE p.id = n.project_id) AS project_kind, ";
    static constexpr const char* kListColumns =
        "n.id, n.slug, n.title, n.summary, n.status, n.topic, n.tags, "
        " n.project_id, n.task_id, "
        " (SELECT p.name FROM projects p WHERE p.id = n.project_id) AS project_title, "
        " (SELECT p.kind FROM projects p WHERE p.id = n.project_id) AS project_kind, "
        " to_char(n.published_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS published_at, "
        " to_char(n.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(n.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";

    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23505") {  // unique_violation: (owner, slug)
            throw SlugTaken();
        }
        if (sqlstate == "23514" || sqlstate == "23503") {  // check, foreign key
            throw Invariant();
        }
    }

    /// A text[] literal; each tag already checked (no quotes, commas or braces reach here unescaped).
    static std::string pg_array(const std::vector<std::string>& tags) {
        std::string out = "{";
        for (const auto& t : tags) {
            if (out.size() > 1) {
                out += ',';
            }
            out += '"';
            for (const char c : t) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                }
                out += c;
            }
            out += '"';
        }
        return out + "}";
    }

    static std::string escape_like(const std::string& q) {
        std::string out;
        for (const char c : q) {
            if (c == '%' || c == '_' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out;
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r =
            txn.exec_params(std::string("SELECT row_to_json(t) FROM (SELECT n.id, n.slug, n.title, n.summary, "
                                        "n.body, n.status, n.topic, n.tags, ") +
                                kLinks + kTimes + " FROM notes n WHERE n.owner_id = $1::uuid AND n.id = $2::uuid) t",
                            owner,
                            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Notes
