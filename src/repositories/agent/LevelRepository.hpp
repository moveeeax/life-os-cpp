/**
 * @file LevelRepository.hpp
 * @brief The facts Ani's level is counted from (domain/AniLevel.hpp): the day the
 *        count starts (ani_level_start, migration 086) and, for each closed
 *        local day since, what the person's tasks, food diary, band, workouts
 *        and money hold. Reads only, except the start day made on the first read.
 *
 * Days are local days in the caller's zone. A day is closed at 04:00 of the
 * next one; the day in progress is not counted.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "database/Database.hpp"
#include "domain/AniLevel.hpp"

namespace Repositories::Agent {

class LevelRepository {
public:
    /// The count starts this many days before the first read.
    static constexpr int kBackfillDays = 90;
    /// The longest stretch of days read at once.
    static constexpr int kMaxDays = 800;

    /// The last closed local day of @p tz: yesterday until 04:00, the day
    /// before it after. Calendar day as YYYY-MM-DD.
    std::string last_closed_day(const std::string& tz) {
        return Database::get().execute_read([&](auto& txn) {
            return txn.exec_params("SELECT (((now() AT TIME ZONE $1) - interval '4 hours')::date - 1)::text", tz)[0][0]
                .template as<std::string>();
        });
    }

    /// The day the count starts from; made on the first call (90 days before
    /// today in @p tz) and kept. Read from the primary: a replica may not have it.
    std::string started_on(const std::string& owner, const std::string& tz) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO ani_level_start (owner_id, started_on) "
                "VALUES ($1::uuid, ((now() AT TIME ZONE $2)::date - $3::int)) ON CONFLICT (owner_id) DO NOTHING",
                owner,
                tz,
                kBackfillDays);
        });
        return Database::get().execute_read_primary([&](auto& txn) {
            return txn
                .exec_params("SELECT started_on::text FROM ani_level_start WHERE owner_id = $1::uuid", owner)[0][0]
                .template as<std::string>();
        });
    }

    /// The days from @p from to @p to (YYYY-MM-DD, both counted), oldest first.
    std::vector<::Domain::AniLevel::Day> days(const std::string& owner,
                                              const std::string& tz,
                                              const std::string& from,
                                              const std::string& to) {
        using ::Domain::AniLevel::Day;
        return Database::get().execute_read([&](auto& txn) {
            std::vector<Day> out;
            std::map<std::string, size_t> at;
            const std::string series = "generate_series($3::date, $4::date, interval '1 day') AS g(d)";
            for (const auto& r : txn.exec_params(
                     "SELECT g.d::date::text, date_trunc('week', g.d)::date::text, "
                     "extract(day from (date_trunc('month', g.d) + interval '1 month' - interval '1 day'))::int "
                     "FROM generate_series($1::date, $2::date, interval '1 day') AS g(d) ORDER BY 1",
                     from,
                     to)) {
                Day d;
                d.date = r[0].template as<std::string>();
                d.week = r[1].template as<std::string>();
                d.days_in_month = r[2].template as<int>();
                at[d.date] = out.size();
                out.push_back(std::move(d));
                if (out.size() >= static_cast<size_t>(kMaxDays)) {
                    break;
                }
            }
            const auto day = [&](const auto& r) -> Day* {
                const auto it = at.find(r[0].template as<std::string>());
                return it == at.end() ? nullptr : &out[it->second];
            };
            // The same bounds for every timestamp read: the local days from..to.
            const std::string lo = "($3::date)::timestamp AT TIME ZONE $2";
            const std::string hi = "(($4::date) + 1)::timestamp AT TIME ZONE $2";

            // Tasks: due, closed by the end of the day, past due then.
            for (const auto& r : txn.exec_params(
                     "SELECT g.d::date::text, "
                     "(SELECT count(*) FROM task_items t WHERE t.owner_id = $1::uuid AND t.due = g.d::date), "
                     "(SELECT count(*) FROM task_items t WHERE t.owner_id = $1::uuid AND t.due = g.d::date "
                     "   AND t.status = 'done' AND (t.completed_at AT TIME ZONE $2)::date <= g.d::date), "
                     "(SELECT count(*) FROM task_items t WHERE t.owner_id = $1::uuid AND t.due < g.d::date "
                     "   AND (t.created_at AT TIME ZONE $2)::date <= g.d::date "
                     "   AND NOT (t.status = 'done' AND (t.completed_at AT TIME ZONE $2)::date <= g.d::date)), "
                     "(SELECT count(*) FROM task_items t WHERE t.owner_id = $1::uuid AND t.due < g.d::date - 3 "
                     "   AND (t.created_at AT TIME ZONE $2)::date <= g.d::date "
                     "   AND NOT (t.status = 'done' AND (t.completed_at AT TIME ZONE $2)::date <= g.d::date)) "
                     "FROM " +
                         series,
                     owner,
                     tz,
                     from,
                     to)) {
                if (Day* d = day(r)) {
                    d->due = r[1].template as<int>();
                    d->closed = r[2].template as<int>();
                    d->overdue = r[3].template as<int>();
                    d->overdue_long = r[4].template as<int>();
                }
            }
            for (const auto& r : txn.exec_params("SELECT date::text, done_at IS NOT NULL FROM day_focus "
                                                 "WHERE owner_id = $1::uuid AND date BETWEEN $2::date AND $3::date",
                                                 owner,
                                                 from,
                                                 to)) {
                if (Day* d = day(r)) {
                    d->focus_done = r[1].template as<bool>();
                }
            }
            for (const auto& r : txn.exec_params("SELECT (started_at AT TIME ZONE $2)::date::text, sum(minutes)::int "
                                                 "FROM focus_sessions WHERE owner_id = $1::uuid AND started_at >= " +
                                                     lo + " AND started_at < " + hi + " GROUP BY 1",
                                                 owner,
                                                 tz,
                                                 from,
                                                 to)) {
                if (Day* d = day(r)) {
                    d->focus_minutes = r[1].template as<int>();
                }
            }

            // Food: the diary and the water.
            for (const auto& r :
                 txn.exec_params("SELECT date::text, count(*)::int, COALESCE(sum(kcal), 0)::float8, "
                                 "COALESCE(sum(protein_g), 0)::float8 FROM food_entries "
                                 "WHERE owner_id = $1::uuid AND date BETWEEN $2::date AND $3::date GROUP BY date",
                                 owner,
                                 from,
                                 to)) {
                if (Day* d = day(r)) {
                    d->entries = r[1].template as<int>();
                    d->kcal = r[2].template as<double>();
                    d->protein_g = r[3].template as<double>();
                }
            }
            for (const auto& r : txn.exec_params("SELECT day::text, ml FROM food_water "
                                                 "WHERE owner_id = $1::uuid AND day BETWEEN $2::date AND $3::date",
                                                 owner,
                                                 from,
                                                 to)) {
                if (Day* d = day(r)) {
                    d->water_ml = r[1].template as<int>();
                }
            }

            // Body: the band's steps and nights (rows are the linked Mi account's),
            // finished workouts, a weighing or a tape.
            for (const auto& r :
                 txn.exec_params("SELECT a.date::text, sum(a.steps)::int FROM daily_activity a "
                                 "JOIN mi_accounts m ON m.xiaomi_user_id = a.user_id "
                                 "WHERE m.owner_id = $1::uuid AND a.date BETWEEN $2::date AND $3::date GROUP BY a.date",
                                 owner,
                                 from,
                                 to)) {
                if (Day* d = day(r)) {
                    d->steps = r[1].template as<int>();
                    d->band_data = d->band_data || d->steps > 0;
                }
            }
            for (const auto& r :
                 txn.exec_params("SELECT (s.end_at AT TIME ZONE $2)::date::text, sum(s.duration_minutes)::int "
                                 "FROM sleep_sessions s JOIN mi_accounts m ON m.xiaomi_user_id = s.user_id "
                                 "WHERE m.owner_id = $1::uuid AND NOT s.is_nap AND s.end_at >= " +
                                     lo + " AND s.end_at < " + hi + " GROUP BY 1",
                                 owner,
                                 tz,
                                 from,
                                 to)) {
                if (Day* d = day(r)) {
                    d->sleep_minutes = r[1].template as<int>();
                    d->band_data = d->band_data || d->sleep_minutes > 0;
                }
            }
            for (const auto& r : txn.exec_params("SELECT (finished_at AT TIME ZONE $2)::date::text, count(*)::int "
                                                 "FROM workout_sessions WHERE owner_id = $1::uuid "
                                                 "AND finished_at IS NOT NULL AND finished_at >= " +
                                                     lo + " AND finished_at < " + hi + " GROUP BY 1",
                                                 owner,
                                                 tz,
                                                 from,
                                                 to)) {
                if (Day* d = day(r)) {
                    d->workouts = r[1].template as<int>();
                }
            }
            for (const auto& r :
                 txn.exec_params("SELECT date::text FROM tape_measurements "
                                 "WHERE owner_id = $1::uuid AND date BETWEEN $3::date AND $4::date "
                                 "UNION SELECT (b.timestamp AT TIME ZONE $2)::date::text FROM body_measurements b "
                                 "JOIN mi_accounts m ON m.xiaomi_user_id = b.user_id WHERE m.owner_id = $1::uuid "
                                 "AND b.timestamp >= " +
                                     lo + " AND b.timestamp < " + hi,
                                 owner,
                                 tz,
                                 from,
                                 to)) {
                if (Day* d = day(r)) {
                    d->measured = true;
                }
            }

            // Money: expenses by currency of the account, and whether a category
            // stood past its monthly budget that day.
            for (const auto& r : txn.exec_params(
                     "SELECT t.date::text, a.currency, sum(t.amount)::float8 FROM money_transactions t "
                     "JOIN money_accounts a ON a.id = t.account_id "
                     "WHERE t.owner_id = $1::uuid AND t.type = 'expense' AND t.date BETWEEN $2::date AND $3::date "
                     "GROUP BY t.date, a.currency",
                     owner,
                     from,
                     to)) {
                if (Day* d = day(r)) {
                    d->spent[r[1].template as<std::string>()] = r[2].template as<double>();
                }
            }
            for (const auto& r : txn.exec_params(
                     "SELECT g.d::date::text FROM generate_series($2::date, $3::date, interval '1 day') AS g(d)"
                     " WHERE EXISTS (SELECT 1 FROM money_categories c WHERE c.owner_id = $1::uuid "
                     "AND c.kind = 'expense' AND NOT c.archived AND c.budget_max IS NOT NULL "
                     "AND c.budget_max < (SELECT COALESCE(sum(t.amount), 0) FROM money_transactions t "
                     "  JOIN money_accounts a ON a.id = t.account_id WHERE t.owner_id = c.owner_id "
                     "  AND t.category_id = c.id AND t.type = 'expense' AND a.currency = c.budget_currency "
                     "  AND t.date >= date_trunc('month', g.d)::date AND t.date <= g.d::date))",
                     owner,
                     from,
                     to)) {
                if (Day* d = day(r)) {
                    d->category_over = true;
                }
            }
            return out;
        });
    }

    /// The categories' monthly budgets summed by currency.
    std::map<std::string, double> monthly_budgets(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            std::map<std::string, double> out;
            for (const auto& r :
                 txn.exec_params("SELECT budget_currency, sum(budget_max)::float8 FROM money_categories "
                                 "WHERE owner_id = $1::uuid AND kind = 'expense' AND NOT archived "
                                 "AND budget_max IS NOT NULL GROUP BY 1",
                                 owner)) {
                out[r[0].template as<std::string>()] = r[1].template as<double>();
            }
            return out;
        });
    }
};

}  // namespace Repositories::Agent
