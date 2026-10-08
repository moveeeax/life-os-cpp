/**
 * @file GoalRepository.hpp
 * @brief goal_items and what hangs on a goal: check-ins of a number goal,
 *        sections of a steps goal, milestones, and the counts of the tasks
 *        linked to it. A binary result closes the goal in the same UPDATE.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "goals/Progress.hpp"
#include "repositories/goals/Errors.hpp"
#include "repositories/tasks/TaskRepository.hpp"

namespace Repositories::Goals {

class GoalRepository {
public:
    struct Input {
        std::string title;
        std::string area;
        std::string kind;
        std::string why;
        std::string start_date;
        std::string due;
        std::string unit;
        std::optional<double> start_value;
        std::optional<double> target_value;
        std::optional<int> target_count;
    };

    struct Patch {
        std::optional<std::string> title;
        std::optional<std::string> area;
        std::optional<std::string> why;
        std::optional<std::string> due;
        std::optional<std::string> status;
        std::optional<std::string> unit;
        std::optional<double> start_value;
        std::optional<double> target_value;
        std::optional<int> target_count;
        std::optional<std::string> result;  // pass | fail; closes the goal
    };

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO goal_items (owner_id, title, area, kind, why, start_date, due, unit, "
                        " start_value, target_value, target_count) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6::date, $7::date, $8, $9, $10, $11) RETURNING id::text",
                        owner,
                        in.title,
                        in.area,
                        in.kind,
                        in.why,
                        in.start_date,
                        in.due,
                        in.unit,
                        in.start_value,
                        in.target_value,
                        in.target_count);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// The owner's goals of one status ("" for all), by due date.
    nlohmann::json list(const std::string& owner, const std::string& status) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM goal_items WHERE owner_id = $1::uuid AND ($2 = '' OR status = $2) "
                                         " ORDER BY due, created_at) t",
                                     owner,
                                     status);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws NotFound, Invariant, InvalidDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    // A result closes the goal; otherwise the status given (or kept).
                    const std::string next_status =
                        "CASE WHEN $12::text IS NOT NULL THEN 'done' ELSE COALESCE($7, status) END";
                    auto r = txn.exec_params(
                        "UPDATE goal_items SET title = COALESCE($3, title), area = COALESCE($4, area), "
                        " why = COALESCE($5, why), due = COALESCE($6::date, due), "
                        " completed_at = CASE WHEN " +
                            next_status +
                            " = 'done' THEN COALESCE(CASE WHEN status = 'done' THEN completed_at END, now()) END, "
                            " status = " +
                            next_status +
                            ", unit = COALESCE($8, unit), start_value = COALESCE($9, start_value), "
                            " target_value = COALESCE($10, target_value), target_count = COALESCE($11, target_count), "
                            " result = COALESCE($12, result), updated_at = now() "
                            "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.title,
                        p.area,
                        p.why,
                        p.due,
                        p.status,
                        p.unit,
                        p.start_value,
                        p.target_value,
                        p.target_count,
                        p.result);
                    if (r.empty()) {
                        throw NotFound("goal");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// The goal and its check-ins, sections and milestones go; its tasks stay, unlinked.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM goal_items WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("goal");
            }
            return true;
        });
    }

    // ── check-ins ──────────────────────────────────────────────────────────

    /// Check-ins oldest first, for the computation.
    std::vector<::Goals::Progress::Checkin> checkin_points(const std::string& owner, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) {
            std::vector<::Goals::Progress::Checkin> out;
            for (const auto& row :
                 txn.exec_params("SELECT to_char(date, 'YYYY-MM-DD'), value::float8 FROM goal_checkins "
                                 "WHERE owner_id = $1::uuid AND goal_id = $2::uuid ORDER BY date",
                                 owner,
                                 goal)) {
                out.push_back(
                    ::Goals::Progress::Checkin{row[0].template as<std::string>(), row[1].template as<double>()});
            }
            return out;
        });
    }

    /// Check-ins newest first, as the page lists them.
    nlohmann::json checkins(const std::string& owner, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kCheckinColumns) +
                    " FROM goal_checkins WHERE owner_id = $1::uuid AND goal_id = $2::uuid ORDER BY date DESC) t",
                owner,
                goal);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// One value per date: a second one for the same date replaces the first.
    /// @throws NotFound, Invariant (not a number goal).
    nlohmann::json put_checkin(const std::string& owner,
                               const std::string& goal,
                               const std::string& date,
                               double value,
                               const std::string& note) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    require_kind_in(txn, owner, goal, "number", "only a number goal takes check-ins");
                    auto r = txn.exec_params(
                        "INSERT INTO goal_checkins (owner_id, goal_id, date, value, note) "
                        "VALUES ($1::uuid, $2::uuid, $3::date, $4, $5) "
                        "ON CONFLICT (goal_id, date) DO UPDATE SET value = EXCLUDED.value, note = EXCLUDED.note "
                        "RETURNING row_to_json((SELECT t FROM (SELECT " +
                            std::string(kCheckinColumns) + ") t))::text",
                        owner,
                        goal,
                        date,
                        value,
                        note);
                    txn.exec_params("UPDATE goal_items SET updated_at = now() WHERE id = $1::uuid", goal);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    void remove_checkin(const std::string& owner, const std::string& goal, const std::string& id) {
        remove_child("goal_checkins", owner, goal, id, "goal_checkin");
    }

    // ── sections ───────────────────────────────────────────────────────────

    nlohmann::json sections(const std::string& owner, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " +
                                         std::string(kSectionColumns) +
                                         " FROM goal_sections WHERE owner_id = $1::uuid AND goal_id = $2::uuid "
                                         " ORDER BY position, created_at) t",
                                     owner,
                                     goal);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws NotFound, Invariant (not a steps goal).
    nlohmann::json add_section(const std::string& owner,
                               const std::string& goal,
                               const std::string& name,
                               std::optional<int> position) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    require_kind_in(txn, owner, goal, "steps", "only a steps goal has sections");
                    auto r = txn.exec_params(
                        "INSERT INTO goal_sections (owner_id, goal_id, name, position) "
                        "VALUES ($1::uuid, $2::uuid, $3, COALESCE($4, (SELECT COALESCE(MAX(position), -1) + 1 "
                        " FROM goal_sections WHERE goal_id = $2::uuid))) "
                        "RETURNING row_to_json((SELECT t FROM (SELECT " +
                            std::string(kSectionColumns) + ") t))::text",
                        owner,
                        goal,
                        name,
                        position);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    nlohmann::json update_section(const std::string& owner,
                                  const std::string& goal,
                                  const std::string& id,
                                  const std::optional<std::string>& name,
                                  std::optional<int> position) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE goal_sections SET name = COALESCE($4, name), position = COALESCE($5, position) "
                        "WHERE owner_id = $1::uuid AND goal_id = $2::uuid AND id = $3::uuid "
                        "RETURNING row_to_json((SELECT t FROM (SELECT " +
                            std::string(kSectionColumns) + ") t))::text",
                        owner,
                        goal,
                        id,
                        name,
                        position);
                    if (r.empty()) {
                        throw NotFound("goal_section");
                    }
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// The section goes; its tasks stay on the goal without a section.
    void remove_section(const std::string& owner, const std::string& goal, const std::string& id) {
        remove_child("goal_sections", owner, goal, id, "goal_section");
    }

    // ── milestones ─────────────────────────────────────────────────────────

    nlohmann::json milestones(const std::string& owner, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " +
                                         std::string(kMilestoneColumns) +
                                         " FROM goal_milestones WHERE owner_id = $1::uuid AND goal_id = $2::uuid "
                                         " ORDER BY date) t",
                                     owner,
                                     goal);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    nlohmann::json add_milestone(const std::string& owner,
                                 const std::string& goal,
                                 const std::string& date,
                                 const std::string& label) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    if (!kind_in(txn, owner, goal).has_value()) {
                        throw NotFound("goal");
                    }
                    auto r = txn.exec_params(
                        "INSERT INTO goal_milestones (owner_id, goal_id, date, label) "
                        "VALUES ($1::uuid, $2::uuid, $3::date, $4) "
                        "RETURNING row_to_json((SELECT t FROM (SELECT " +
                            std::string(kMilestoneColumns) + ") t))::text",
                        owner,
                        goal,
                        date,
                        label);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    void remove_milestone(const std::string& owner, const std::string& goal, const std::string& id) {
        remove_child("goal_milestones", owner, goal, id, "goal_milestone");
    }

    // ── tasks of goals ─────────────────────────────────────────────────────

    /// Goal id -> its tasks done and total, for every goal of the owner that has tasks.
    std::map<std::string, ::Goals::Progress::Counts> task_counts(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::map<std::string, ::Goals::Progress::Counts> out;
            for (const auto& row : txn.exec_params(
                     "SELECT goal_id::text, count(*) FILTER (WHERE status = 'done'), count(*) FROM task_items "
                     "WHERE owner_id = $1::uuid AND goal_id IS NOT NULL GROUP BY goal_id",
                     owner)) {
                out[row[0].template as<std::string>()] =
                    ::Goals::Progress::Counts{row[1].template as<int>(), row[2].template as<int>()};
            }
            return out;
        });
    }

    /// The goal's tasks, in section order then by creation.
    nlohmann::json tasks_of(const std::string& owner, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " +
                    std::string(Repositories::Tasks::TaskRepository::kColumns) +
                    " FROM task_items WHERE owner_id = $1::uuid AND goal_id = $2::uuid "
                    " ORDER BY (SELECT s.position FROM goal_sections s WHERE s.id = task_items.goal_section_id) "
                    " NULLS LAST, created_at) t",
                owner,
                goal);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The kind of the owner's goal, or nullopt when it is not theirs.
    template <typename Txn>
    static std::optional<std::string> kind_in(Txn& txn, const std::string& owner, const std::string& goal) {
        auto r =
            txn.exec_params("SELECT kind FROM goal_items WHERE owner_id = $1::uuid AND id = $2::uuid", owner, goal);
        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    }

    std::optional<std::string> kind_of(const std::string& owner, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) { return kind_in(txn, owner, goal); });
    }

    /// Whether the section belongs to that goal of the owner.
    bool section_of_goal(const std::string& owner, const std::string& section, const std::string& goal) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM goal_sections WHERE owner_id = $1::uuid AND id = $2::uuid AND goal_id = "
                           "$3::uuid",
                           owner,
                           section,
                           goal)
                        .empty();
        });
    }

    /// The computation's view of a goal row as the API shows it.
    static ::Goals::Progress::Goal progress_goal(const nlohmann::json& row) {
        const auto num = [&](const char* k) { return row[k].is_number() ? row[k].get<double>() : 0.0; };
        return ::Goals::Progress::Goal{row["kind"].get<std::string>(),
                                       row["start_date"].get<std::string>(),
                                       row["due"].get<std::string>(),
                                       num("start_value"),
                                       num("target_value"),
                                       row["target_count"].is_number() ? row["target_count"].get<int>() : 0,
                                       row["result"].is_string() ? row["result"].get<std::string>() : ""};
    }

    static constexpr const char* kColumns =
        "id, title, area, kind, why, to_char(start_date, 'YYYY-MM-DD') AS start_date, "
        " to_char(due, 'YYYY-MM-DD') AS due, status, "
        " to_char(completed_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS completed_at, "
        " unit, start_value::float8 AS start_value, target_value::float8 AS target_value, target_count, result, "
        " to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";
    static constexpr const char* kCheckinColumns =
        "id, to_char(date, 'YYYY-MM-DD') AS date, value::float8 AS value, note";
    static constexpr const char* kSectionColumns = "id, name, position";
    static constexpr const char* kMilestoneColumns = "id, to_char(date, 'YYYY-MM-DD') AS date, label";

private:
    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM goal_items WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }

    template <typename Txn>
    static void require_kind_in(
        Txn& txn, const std::string& owner, const std::string& goal, const char* kind, const char* message) {
        const auto k = kind_in(txn, owner, goal);
        if (!k.has_value()) {
            throw NotFound("goal");
        }
        if (*k != kind) {
            throw Invariant(message);
        }
    }

    /// Deletes a row of a child table that belongs to the owner's goal. @throws NotFound.
    static void remove_child(
        const char* table, const std::string& owner, const std::string& goal, const std::string& id, const char* what) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(std::string("DELETE FROM ") + table +
                                         " WHERE owner_id = $1::uuid AND goal_id = $2::uuid AND id = $3::uuid "
                                         "RETURNING id",
                                     owner,
                                     goal,
                                     id);
            if (r.empty()) {
                throw NotFound(what);
            }
            return true;
        });
    }
};

}  // namespace Repositories::Goals
