/**
 * @file BriefRepository.hpp
 * @brief The counts of a person's day that Ani's voice line to Telegram says
 *        (agent/VoiceBrief.hpp): tasks due, overdue and done that day, and
 *        the daily habits with their ticks.
 */

#pragma once

#include <array>
#include <string>

#include "database/Database.hpp"

namespace Repositories::Agent {

class BriefRepository {
public:
    /// {due today, overdue, done today, daily habits, ticked today} on the local @p day in @p tz.
    std::array<int, 5> day_counts(const std::string& owner, const std::string& day, const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT "
                "(SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND status <> 'done' AND due = "
                "$2::date), "
                "(SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND status <> 'done' AND due < "
                "$2::date), "
                "(SELECT count(*) FROM task_items WHERE owner_id = $1::uuid AND status = 'done' AND (completed_at "
                "AT TIME ZONE $3)::date = $2::date), "
                "(SELECT count(*) FROM habits WHERE owner_id = $1::uuid AND archived_at IS NULL AND schedule = "
                "'daily'), "
                "(SELECT count(*) FROM habit_checks c JOIN habits h ON h.id = c.habit_id WHERE c.owner_id = "
                "$1::uuid AND c.date = $2::date AND h.archived_at IS NULL AND h.schedule = 'daily')",
                owner,
                day,
                tz);
            return std::array<int, 5>{r[0][0].template as<int>(),
                                      r[0][1].template as<int>(),
                                      r[0][2].template as<int>(),
                                      r[0][3].template as<int>(),
                                      r[0][4].template as<int>()};
        });
    }
};

}  // namespace Repositories::Agent
