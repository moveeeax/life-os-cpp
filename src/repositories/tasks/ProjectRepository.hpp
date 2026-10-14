/**
 * @file ProjectRepository.hpp
 * @brief The owner's projects — streams of work that group tasks — and the
 *        courses of Learning, which are projects of kind "course". A list row
 *        carries its open and done task counts and the next open task.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/tasks/Errors.hpp"

namespace Repositories::Tasks {

struct CodeTaken : ConflictError {
    CodeTaken() : ConflictError("code_taken", "another project of yours has this code") {}
};

class ProjectRepository {
public:
    struct Input {
        std::string kind = "project";  // project | course
        std::string name;
        std::string code;
        std::string description;
        std::string link;
        std::string status;  // project: active | paused | done; course: planned | active | done
        std::string platform;
        std::optional<std::string> starts_on;
        std::optional<std::string> ends_on;
        std::string certificate;
    };

    /// A partial edit: a field left empty is kept; a date given as null clears.
    struct Patch {
        std::optional<std::string> name;
        std::optional<std::string> code;
        std::optional<std::string> description;
        std::optional<std::string> link;
        std::optional<std::string> status;
        std::optional<std::string> platform;
        std::optional<std::optional<std::string>> starts_on;
        std::optional<std::optional<std::string>> ends_on;
        std::optional<std::string> certificate;
    };

    /// @throws CodeTaken, Invariant, InvalidDate.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO projects (owner_id, kind, name, code, description, link, status, platform, "
                        " starts_on, ends_on, certificate, position) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9::date, $10::date, $11, "
                        " (SELECT COALESCE(max(position), 0) + 1 FROM projects WHERE owner_id = $1::uuid)) "
                        "RETURNING id::text",
                        owner,
                        in.kind,
                        in.name,
                        in.code,
                        in.description,
                        in.link,
                        in.status,
                        in.platform,
                        in.starts_on,
                        in.ends_on,
                        in.certificate);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// Whether the project or course is the owner's.
    bool owns(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params("SELECT 1 FROM projects WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id)
                        .empty();
        });
    }

    /// @throws NotFound, CodeTaken, Invariant, InvalidDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE projects SET name = COALESCE($3, name), code = COALESCE($4, code), "
                        " description = COALESCE($5, description), link = COALESCE($6, link), "
                        " status = COALESCE($7, status), platform = COALESCE($8, platform), "
                        " starts_on = CASE WHEN $9::boolean THEN $10::date ELSE starts_on END, "
                        " ends_on = CASE WHEN $11::boolean THEN $12::date ELSE ends_on END, "
                        " certificate = COALESCE($13, certificate), updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.name,
                        p.code,
                        p.description,
                        p.link,
                        p.status,
                        p.platform,
                        p.starts_on.has_value(),
                        p.starts_on.has_value() ? *p.starts_on : std::optional<std::string>(),
                        p.ends_on.has_value(),
                        p.ends_on.has_value() ? *p.ends_on : std::optional<std::string>(),
                        p.certificate);
                    if (r.empty()) {
                        throw NotFound("project");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &translate);
    }

    /// Its tasks stay, unlinked (ON DELETE SET NULL). @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM projects WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("project");
            }
            return true;
        });
    }

    /// Projects of @p kind ("" any) and @p status ("" any) in their order.
    nlohmann::json list(const std::string& owner, const std::string& kind, const std::string& status) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM projects p WHERE p.owner_id = $1::uuid AND ($2 = '' OR p.kind = $2) "
                                         " AND ($3 = '' OR p.status = $3) ORDER BY p.position, p.created_at) t",
                                     owner,
                                     kind,
                                     status);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

private:
    /// The columns of a project, its task counts and the next open task (the earliest due, then the oldest);
    /// open means not done, so a task in progress counts.
    static constexpr const char* kColumns =
        "p.id, p.kind, p.name, p.code, p.description, p.link, p.status, p.platform, "
        " to_char(p.starts_on, 'YYYY-MM-DD') AS starts_on, to_char(p.ends_on, 'YYYY-MM-DD') AS ends_on, "
        " p.certificate, p.position, "
        " (SELECT count(*) FROM task_items t WHERE t.project_id = p.id AND t.status <> 'done') AS open, "
        " (SELECT count(*) FROM task_items t WHERE t.project_id = p.id AND t.status = 'done') AS done, "
        " (SELECT json_build_object('id', t.id, 'title', t.title, 'due', to_char(t.due, 'YYYY-MM-DD')) "
        "   FROM task_items t WHERE t.project_id = p.id AND t.status <> 'done' "
        "   ORDER BY t.due NULLS LAST, t.created_at LIMIT 1) AS next, "
        " to_char(p.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(p.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";

    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23505") {  // unique_violation: the (owner, code) index
            throw CodeTaken();
        }
        detail::translate(sqlstate);
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM projects p WHERE p.owner_id = $1::uuid AND p.id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Tasks
