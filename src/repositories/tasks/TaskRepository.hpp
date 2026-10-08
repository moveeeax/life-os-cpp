/**
 * @file TaskRepository.hpp
 * @brief task_items: the person's tasks. Closing sets completed_at and
 *        reopening clears it in the same UPDATE; the agenda rows come back
 *        with the day read in the caller's zone.
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/tasks/Errors.hpp"
#include "tasks/Agenda.hpp"

namespace Repositories::Tasks {

class TaskRepository {
public:
    struct Input {
        std::string title;
        std::string area;
        std::optional<std::string> effort;
        std::optional<std::string> due;
        std::string next_step;
        std::string note;
        std::optional<std::string> source_kind;
        std::optional<std::string> source_ref;
        std::optional<std::string> external_id;
        std::string status = "open";
        std::optional<std::string> goal_id;
        std::optional<std::string> goal_section_id;
    };

    struct Patch {
        std::optional<std::string> title;
        std::optional<std::string> area;
        std::optional<std::optional<std::string>> effort;  // outer: given; inner: null clears
        std::optional<std::optional<std::string>> due;
        std::optional<std::string> next_step;
        std::optional<std::string> note;
        std::optional<std::string> status;
        std::optional<std::optional<std::string>> source_kind;
        std::optional<std::optional<std::string>> source_ref;
        std::optional<std::optional<std::string>> goal_id;
        std::optional<std::optional<std::string>> goal_section_id;
    };

    struct Filter {
        std::string status;      // "" any, open, done
        std::string area;        // "" any
        std::string q;           // over title and next step
        std::string due_to;      // "" or YYYY-MM-DD
        std::string done_since;  // "" or YYYY-MM-DD (completed_at >= that day, UTC)
        int limit = 50;
        int offset = 0;
    };

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write(
                    [&](auto& txn) { return *find_in(txn, owner, create_in(txn, owner, in)); });
            },
            &detail::translate);
    }

    template <typename Txn>
    static std::string create_in(Txn& txn, const std::string& owner, const Input& in) {
        auto r = txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, effort, due, next_step, note, status, completed_at, "
            " source_kind, source_ref, external_id, goal_id, goal_section_id) "
            "VALUES ($1::uuid, $2, $3, $4, $5::date, $6, $7, $8, CASE WHEN $8 = 'done' THEN now() END, $9, $10, $11, "
            " $12::uuid, $13::uuid) "
            "RETURNING id::text",
            owner,
            in.title,
            in.area,
            in.effort,
            in.due,
            in.next_step,
            in.note,
            in.status,
            in.source_kind,
            in.source_ref,
            in.external_id,
            in.goal_id,
            in.goal_section_id);
        return r[0][0].template as<std::string>();
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// @throws NotFound, Invariant, InvalidDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE task_items SET title = COALESCE($3, title), area = COALESCE($4, area), "
                        " effort = CASE WHEN $5::boolean THEN $6 ELSE effort END, "
                        " due = CASE WHEN $7::boolean THEN $8::date ELSE due END, "
                        " next_step = COALESCE($9, next_step), note = COALESCE($10, note), "
                        " status = COALESCE($11, status), "
                        " completed_at = CASE WHEN COALESCE($11, status) = 'done' "
                        "   THEN COALESCE(CASE WHEN status = 'done' THEN completed_at END, now()) END, "
                        " source_kind = CASE WHEN $12::boolean THEN $13 ELSE source_kind END, "
                        " source_ref = CASE WHEN $14::boolean THEN $15 ELSE source_ref END, "
                        " goal_id = CASE WHEN $16::boolean THEN $17::uuid ELSE goal_id END, "
                        " goal_section_id = CASE WHEN $18::boolean THEN $19::uuid ELSE goal_section_id END, "
                        " updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.title,
                        p.area,
                        p.effort.has_value(),
                        p.effort.has_value() ? *p.effort : std::optional<std::string>(),
                        p.due.has_value(),
                        p.due.has_value() ? *p.due : std::optional<std::string>(),
                        p.next_step,
                        p.note,
                        p.status,
                        p.source_kind.has_value(),
                        p.source_kind.has_value() ? *p.source_kind : std::optional<std::string>(),
                        p.source_ref.has_value(),
                        p.source_ref.has_value() ? *p.source_ref : std::optional<std::string>(),
                        p.goal_id.has_value(),
                        p.goal_id.has_value() ? *p.goal_id : std::optional<std::string>(),
                        p.goal_section_id.has_value(),
                        p.goal_section_id.has_value() ? *p.goal_section_id : std::optional<std::string>());
                    if (r.empty()) {
                        throw NotFound("task");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM task_items WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("task");
            }
            return true;
        });
    }

    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            const std::string where =
                " WHERE owner_id = $1::uuid AND ($2 = '' OR status = $2) AND ($3 = '' OR area = $3) "
                " AND ($4 = '' OR title ILIKE '%' || $4 || '%' OR next_step ILIKE '%' || $4 || '%') "
                " AND ($5 = '' OR due <= $5::date) AND ($6 = '' OR completed_at >= $6::date) ";
            auto total = txn.exec_params(
                "SELECT count(*) FROM task_items" + where, owner, f.status, f.area, f.q, f.due_to, f.done_since);
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM task_items" + where + " ORDER BY created_at DESC LIMIT $7 OFFSET $8) t",
                                     owner,
                                     f.status,
                                     f.area,
                                     f.q,
                                     f.due_to,
                                     f.done_since,
                                     f.limit,
                                     f.offset);
            return nlohmann::json{{"data", nlohmann::json::parse(r[0][0].template as<std::string>())},
                                  {"total", total[0][0].template as<long>()}};
        });
    }

    /// Open tasks plus those closed on or after the day before @p date, the day read in @p tz.
    std::vector<::Tasks::Agenda::Item> agenda_items(const std::string& owner,
                                                    const std::string& date,
                                                    const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT id::text, status, COALESCE(to_char(due, 'YYYY-MM-DD'), ''), "
                " COALESCE(to_char(completed_at AT TIME ZONE $3, 'YYYY-MM-DD'), ''), next_step, "
                " ($2::date - (created_at AT TIME ZONE $3)::date), ($2::date - (updated_at AT TIME ZONE $3)::date), "
                " (SELECT row_to_json(t) FROM (SELECT " +
                    std::string(kColumns) +
                    ") t)::text, goal_id IS NOT NULL "
                    "FROM task_items WHERE owner_id = $1::uuid AND (status <> 'done' OR "
                    " (completed_at AT TIME ZONE $3)::date >= $2::date - 1) "
                    "ORDER BY due NULLS LAST, created_at",
                owner,
                date,
                tz);
            std::vector<::Tasks::Agenda::Item> out;
            for (const auto& row : r) {
                out.push_back(::Tasks::Agenda::Item{row[0].template as<std::string>(),
                                                    row[1].template as<std::string>(),
                                                    row[2].template as<std::string>(),
                                                    row[3].template as<std::string>(),
                                                    row[4].template as<std::string>(),
                                                    row[5].template as<int>(),
                                                    row[6].template as<int>(),
                                                    nlohmann::json::parse(row[7].template as<std::string>()),
                                                    row[8].template as<bool>()});
            }
            return out;
        });
    }

    /// Lower-cased titles of the owner's open tasks of the last 30 days, for the parse's duplicate flag.
    std::set<std::string> open_titles(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::set<std::string> out;
            for (const auto& row :
                 txn.exec_params("SELECT lower(title) FROM task_items WHERE owner_id = $1::uuid AND status <> 'done' "
                                 " AND created_at > now() - interval '30 days'",
                                 owner)) {
                out.insert(row[0].template as<std::string>());
            }
            return out;
        });
    }

    static bool is_timezone(const std::string& tz) {
        return Database::get().execute_read(
            [&](auto& txn) { return !txn.exec_params("SELECT 1 FROM pg_timezone_names WHERE name = $1", tz).empty(); });
    }

    /// Whether a money ledger row is the owner's (the table exists whether the module is on or not).
    bool money_row_is_owners(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM money_transactions WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id)
                        .empty();
        });
    }

    static constexpr const char* kColumns =
        "id, title, area, effort, to_char(due, 'YYYY-MM-DD') AS due, next_step, note, status, "
        " to_char(completed_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS completed_at, "
        " source_kind, source_ref, external_id, goal_id, goal_section_id, "
        " (SELECT g.title FROM goal_items g WHERE g.id = task_items.goal_id) AS goal_title, "
        " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";

    /// The task as the API shows it; public for the parse accept, which creates tasks in its own transaction.
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM task_items WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Tasks
