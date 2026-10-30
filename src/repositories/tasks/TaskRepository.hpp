/**
 * @file TaskRepository.hpp
 * @brief task_items: the person's tasks. Closing sets completed_at and
 *        reopening clears it in the same UPDATE; closing a task with a repeat
 *        rule creates its next occurrence in the same transaction; the agenda
 *        rows come back with the day read in the caller's zone.
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/LikePattern.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/Zones.hpp"
#include "repositories/tasks/Errors.hpp"
#include "tasks/Agenda.hpp"
#include "tasks/Repeat.hpp"

namespace Repositories::Tasks {

class TaskRepository {
public:
    struct Input {
        std::string title;
        std::string area;
        std::optional<std::string> effort;
        std::optional<std::string> due;
        /// HH:MM in the person's zone, only with a due day (the day ribbon pins it).
        std::optional<std::string> due_time;
        std::string next_step;
        std::string note;
        std::optional<std::string> source_kind;
        std::optional<std::string> source_ref;
        std::optional<std::string> external_id;
        std::string status = "open";
        std::optional<std::string> goal_id;
        std::optional<std::string> goal_section_id;
        std::optional<std::string> trip_id;
        std::optional<std::string> project_id;
        std::optional<std::string> repeat;  // the rule as JSON text (Tasks::Repeat::to_json)
        std::optional<std::string> deal_id;
    };

    struct Patch {
        std::optional<std::string> title;
        std::optional<std::string> area;
        std::optional<std::optional<std::string>> effort;  // outer: given; inner: null clears
        std::optional<std::optional<std::string>> due;
        std::optional<std::optional<std::string>> due_time;
        std::optional<std::string> next_step;
        std::optional<std::string> note;
        std::optional<std::string> status;
        std::optional<std::optional<std::string>> source_kind;
        std::optional<std::optional<std::string>> source_ref;
        std::optional<std::optional<std::string>> goal_id;
        std::optional<std::optional<std::string>> goal_section_id;
        std::optional<std::optional<std::string>> trip_id;
        std::optional<std::optional<std::string>> project_id;
        std::optional<std::optional<std::string>> repeat;  // JSON text; null stops the repeat
        /// The local day the task is closed on, for the next occurrence; empty: today in UTC.
        std::optional<std::string> today;
        std::optional<std::optional<std::string>> deal_id;
    };

    struct Filter {
        std::string status;      // "" any, open, done
        std::string area;        // "" any
        std::string q;           // over title and next step
        std::string due_to;      // "" or YYYY-MM-DD
        std::string done_since;  // "" or YYYY-MM-DD (completed_at >= the start of that day in tz)
        int limit = 50;
        int offset = 0;
        std::string trip;        // "" any, or a trip id: that trip's tasks (packing)
        std::string project;     // "" any, or a project (or course) id
        std::string deal;        // "" any, or a deal id of the practice: its next steps
        std::string tz = "UTC";  // the zone done_since is read in (a zone PostgreSQL knows)
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
            " source_kind, source_ref, external_id, goal_id, goal_section_id, trip_id, project_id, repeat, deal_id, "
            "due_time) "
            "VALUES ($1::uuid, $2, $3, $4, $5::date, $6, $7, $8, CASE WHEN $8 = 'done' THEN now() END, $9, $10, $11, "
            " $12::uuid, $13::uuid, $14::uuid, $15::uuid, $16::jsonb, $17::uuid, $18::time) "
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
            in.goal_section_id,
            in.trip_id,
            in.project_id,
            in.repeat,
            in.deal_id,
            in.due ? in.due_time : std::optional<std::string>());
        return r[0][0].template as<std::string>();
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// @throws NotFound, Invariant, InvalidDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] { return Database::get().execute_write([&](auto& txn) { return update_in(txn, owner, id, p); }); },
            &detail::translate);
    }

    /// update() inside the caller's transaction (the focus of the day closes
    /// its task in the same one). A repeating task that goes to done gets its
    /// next occurrence here, once. The SQLSTATE translation is the caller's:
    /// wrap in detail::translate_sql. @throws NotFound.
    template <typename Txn>
    static nlohmann::json update_in(Txn& txn, const std::string& owner, const std::string& id, const Patch& p) {
        auto before = txn.exec_params(
            "SELECT status, recur_next_id IS NOT NULL FROM task_items "
            "WHERE owner_id = $1::uuid AND id = $2::uuid FOR UPDATE",
            owner,
            id);
        if (before.empty()) {
            throw NotFound("task");
        }
        const bool was_done = before[0][0].template as<std::string>() == "done";
        const bool has_next = before[0][1].template as<bool>();
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
            " trip_id = CASE WHEN $20::boolean THEN $21::uuid ELSE trip_id END, "
            " project_id = CASE WHEN $22::boolean THEN $23::uuid ELSE project_id END, "
            " repeat = CASE WHEN $24::boolean THEN $25::jsonb ELSE repeat END, "
            " deal_id = CASE WHEN $26::boolean THEN $27::uuid ELSE deal_id END, "
            // A time without a day means nothing: no day after the patch, no time.
            " due_time = CASE WHEN (CASE WHEN $7::boolean THEN $8::date ELSE due END) IS NULL THEN NULL "
            "   WHEN $28::boolean THEN $29::time ELSE due_time END "
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
            p.goal_section_id.has_value() ? *p.goal_section_id : std::optional<std::string>(),
            p.trip_id.has_value(),
            p.trip_id.has_value() ? *p.trip_id : std::optional<std::string>(),
            p.project_id.has_value(),
            p.project_id.has_value() ? *p.project_id : std::optional<std::string>(),
            p.repeat.has_value(),
            p.repeat.has_value() ? *p.repeat : std::optional<std::string>(),
            p.deal_id.has_value(),
            p.deal_id.has_value() ? *p.deal_id : std::optional<std::string>(),
            p.due_time.has_value(),
            p.due_time.has_value() ? *p.due_time : std::optional<std::string>());
        if (r.empty()) {
            throw NotFound("task");
        }
        auto row = *find_in(txn, owner, id);
        if (!was_done && !has_next && row["status"] == "done" && row["repeat"].is_object()) {
            if (spawn_next_in(txn, owner, id, row, p.today)) {
                row = *find_in(txn, owner, id);
            }
        }
        return row;
    }

    /// The next occurrence of the closed task @p row: a copy, open, due on the
    /// rule's next day, with no external id; the closed one points at it.
    /// False when the stored rule does not read (left alone, the close stands).
    template <typename Txn>
    static bool spawn_next_in(Txn& txn,
                              const std::string& owner,
                              const std::string& id,
                              const nlohmann::json& row,
                              const std::optional<std::string>& today) {
        ::Tasks::Repeat::Rule rule;
        if (::Tasks::Repeat::check(row["repeat"], rule)) {
            return false;
        }
        const std::string done_day =
            today ? *today
                  : txn.exec("SELECT to_char((now() AT TIME ZONE 'UTC')::date, 'YYYY-MM-DD')")[0][0]
                        .template as<std::string>();
        const std::string due = row["due"].is_string() ? row["due"].get<std::string>() : "";
        const std::string next = ::Tasks::Repeat::next_due(rule, due, done_day);
        auto made = txn.exec_params(
            "INSERT INTO task_items (owner_id, title, area, effort, due, next_step, note, status, source_kind, "
            " source_ref, goal_id, goal_section_id, trip_id, project_id, repeat, deal_id, due_time) "
            "SELECT owner_id, title, area, effort, $3::date, next_step, note, 'open', source_kind, source_ref, "
            " goal_id, goal_section_id, trip_id, project_id, repeat, deal_id, due_time "
            "FROM task_items WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id::text",
            owner,
            id,
            next);
        txn.exec_params("UPDATE task_items SET recur_next_id = $3::uuid WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        id,
                        made[0][0].template as<std::string>());
        return true;
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
                " AND ($4 = '' OR title ILIKE '%' || $4 || '%' ESCAPE '\\' OR next_step ILIKE '%' || $4 || '%' ESCAPE "
                "'\\') "
                " AND ($5 = '' OR due <= $5::date) AND ($6 = '' OR completed_at >= ($6::date::timestamp AT TIME ZONE "
                "$11)) AND ($9 = '' OR trip_id = NULLIF($9, '')::uuid) "
                " AND ($10 = '' OR project_id = NULLIF($10, '')::uuid) "
                " AND ($12 = '' OR deal_id = NULLIF($12, '')::uuid) ";
            const std::string q = escape_like(f.q);
            auto total =
                txn.exec_params("SELECT count(*) FROM task_items" + where + " AND $7::int >= 0 AND $8::int >= 0",
                                owner,
                                f.status,
                                f.area,
                                q,
                                f.due_to,
                                f.done_since,
                                f.limit,
                                f.offset,
                                f.trip,
                                f.project,
                                f.tz,
                                f.deal);
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM task_items" + where + " ORDER BY created_at DESC LIMIT $7 OFFSET $8) t",
                                     owner,
                                     f.status,
                                     f.area,
                                     q,
                                     f.due_to,
                                     f.done_since,
                                     f.limit,
                                     f.offset,
                                     f.trip,
                                     f.project,
                                     f.tz,
                                     f.deal);
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
                    // Dated by date; undated (someday) newest first.
                    "ORDER BY due NULLS LAST, CASE WHEN due IS NULL THEN created_at END DESC, created_at",
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

    /// The zone names PostgreSQL knows, read once per process (the list only changes with a server upgrade).
    static bool is_timezone(const std::string& tz) { return Zones::is_zone(tz); }

    /// Whether a money ledger row is the owner's (the table exists whether the module is on or not).
    bool money_row_is_owners(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM money_transactions WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id)
                        .empty();
        });
    }

    static constexpr const char* kColumns =
        "id, title, area, effort, to_char(due, 'YYYY-MM-DD') AS due, to_char(due_time, 'HH24:MI') AS due_time, "
        " next_step, note, status, "
        " " SQL_ISO_UTC("completed_at") " AS completed_at, "
        " source_kind, source_ref, external_id, goal_id, goal_section_id, trip_id, project_id, "
        " repeat, recur_next_id AS next_id, deal_id, "
        " (SELECT d.title FROM practice_deals d WHERE d.id = task_items.deal_id) AS deal_title, "
        " (SELECT g.title FROM goal_items g WHERE g.id = task_items.goal_id) AS goal_title, "
        " (SELECT p.name FROM projects p WHERE p.id = task_items.project_id) AS project_title, "
        " (SELECT p.kind FROM projects p WHERE p.id = task_items.project_id) AS project_kind, "
        " " SQL_ISO_UTC("created_at") " AS created_at, "
        " " SQL_ISO_UTC("updated_at") " AS updated_at";

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
