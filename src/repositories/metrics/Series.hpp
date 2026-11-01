/**
 * @file Series.hpp
 * @brief One series of the metrics catalog (metrics/Catalog.hpp) read for a
 *        range of days: one value per local day, one SQL per family of
 *        entries. A row with a timestamptz lands on its day in the request
 *        zone (`(ts AT TIME ZONE tz)::date`, read with a day of padding on
 *        each side); a row with a date column lands on that date. Every
 *        statement filters by the owner (the Mi tables by the owner's Xiaomi
 *        id), and an id naming a category, a habit or a currency that is not
 *        the owner's reads as not_yours with no values.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "habits/Habits.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/leetcode/ProfileRepository.hpp"
#include "repositories/money/SummaryRepository.hpp"
#include "utils/Modules.hpp"

namespace Repositories::Metrics {

/// What the caller may read: the module switches, the fitness read permission,
/// and the Xiaomi id of the linked Mi account (empty without a link, or when the
/// Mi tables are not readable anyway).
struct Access {
    bool fitness = false;
    bool mi_read = false;
    bool workout = false;
    bool food = false;
    bool habits = false;
    bool tasks = false;
    bool money = false;
    bool practice = false;
    /// A LeetCode username is set (leetcode_profiles).
    bool leetcode = false;
    std::string xiaomi_id;
};

/**
 * The access of @p owner holding @p permissions (Domain::Permission bits; the
 * admin bit counts as every bit). The Mi link is looked up only when the Mi
 * tables are readable.
 */
inline Access access_of(const std::string& owner, std::uint32_t permissions) {
    Access a;
    a.fitness = Core::fitness_enabled();
    a.mi_read = (permissions & Domain::Permission::kFitnessRead) != 0 ||
                (permissions & Domain::Permission::kAdminister) == Domain::Permission::kAdminister;
    a.workout = Core::workout_enabled();
    a.food = Core::food_enabled();
    a.habits = Core::habits_enabled();
    a.tasks = Core::tasks_enabled();
    a.money = Core::money_enabled();
    a.practice = Core::practice_enabled();
    a.leetcode = Repositories::LeetCode::ProfileRepository().exists(owner);
    if (a.fitness && a.mi_read) {
        a.xiaomi_id = Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid", owner);
            return r.empty() ? std::string() : r[0][0].template as<std::string>();
        });
    }
    return a;
}

/// Why a series has no values for the caller (Ok: it has, or the days are empty).
enum class Why { Ok, ModuleOff, NoPermission, NotLinked, NotYours };

/// The API name of @p w: ok, module_off, no_permission, not_linked, not_yours.
inline std::string_view why_name(Why w) {
    switch (w) {
        case Why::Ok:
            return "ok";
        case Why::ModuleOff:
            return "module_off";
        case Why::NoPermission:
            return "no_permission";
        case Why::NotLinked:
            return "not_linked";
        case Why::NotYours:
            return "not_yours";
    }
    return "ok";
}

namespace detail {

/// Whether @p owner has a money account in @p currency, and (when given) an expense
/// category @p category; a habit @p habit ticked by hand.
inline bool owns(const std::string& owner, const ::Metrics::Ref& r) {
    // Only a currency, a category or a habit id has an owner to check; the other entries
    // open no transaction.
    const bool parametrised = r.def->gate == ::Metrics::Gate::Money ||
                              (r.def->gate == ::Metrics::Gate::Habits && r.def->param == ::Metrics::Param::Habit);
    if (!parametrised) {
        return true;
    }
    return Database::get().execute_read([&](auto& txn) {
        switch (r.def->gate) {
            case ::Metrics::Gate::Money: {
                auto acc =
                    txn.exec_params("SELECT 1 FROM money_accounts WHERE owner_id = $1::uuid AND currency = $2 LIMIT 1",
                                    owner,
                                    r.currency);
                if (acc.empty()) {
                    return false;
                }
                if (r.def->param == ::Metrics::Param::CurrencyCategory) {
                    return !txn.exec_params(
                                   "SELECT 1 FROM money_categories WHERE owner_id = $1::uuid AND id = $2::uuid",
                                   owner,
                                   r.category_id)
                                .empty();
                }
                return true;
            }
            case ::Metrics::Gate::Habits:
                if (r.def->param == ::Metrics::Param::Habit) {
                    return !txn.exec_params(
                                   "SELECT 1 FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid "
                                   "AND source = 'manual'",
                                   owner,
                                   r.habit_id)
                                .empty();
                }
                return true;
            default:
                return true;
        }
    });
}

}  // namespace detail

/**
 * Whether the caller may read the entry @p d at all, before its parameters: the
 * module first (module_off), then for a Mi series the permission
 * (no_permission) and the link (not_linked).
 */
inline Why gate_available(const ::Metrics::Def& d, const Access& a) {
    using ::Metrics::Gate;
    switch (d.gate) {
        case Gate::Mi:
            if (!a.fitness) {
                return Why::ModuleOff;
            }
            if (!a.mi_read) {
                return Why::NoPermission;
            }
            return a.xiaomi_id.empty() ? Why::NotLinked : Why::Ok;
        case Gate::Tape:
            return a.fitness ? Why::Ok : Why::ModuleOff;
        case Gate::Food:
            return a.food ? Why::Ok : Why::ModuleOff;
        case Gate::Habits:
            return a.habits ? Why::Ok : Why::ModuleOff;
        case Gate::Workout:
            return a.workout ? Why::Ok : Why::ModuleOff;
        case Gate::Tasks:
            return a.tasks ? Why::Ok : Why::ModuleOff;
        case Gate::Money:
            return a.money ? Why::Ok : Why::ModuleOff;
        case Gate::Practice:
            return a.practice ? Why::Ok : Why::ModuleOff;
        case Gate::LeetCode:
            return a.leetcode ? Why::Ok : Why::NotLinked;
    }
    return Why::ModuleOff;
}

/**
 * Whether @p owner may read @p r: gate_available(), then the parameters
 * (not_yours: a currency without a money account of the owner, a category or a
 * habit of someone else, a habit with a source of its own).
 */
inline Why available(const std::string& owner, const ::Metrics::Ref& r, const Access& a) {
    if (r.def == nullptr) {
        return Why::NotYours;
    }
    const Why gate = gate_available(*r.def, a);
    if (gate != Why::Ok) {
        return gate;
    }
    return detail::owns(owner, r) ? Why::Ok : Why::NotYours;
}

/// The unit of the series @p r: the catalog's (the currency for money and
/// practice), the habit's own unit for `habits:<id>`.
inline std::string unit_for(const std::string& owner, const ::Metrics::Ref& r) {
    if (r.def == nullptr || r.def->param != ::Metrics::Param::Habit) {
        return ::Metrics::unit_of(r);
    }
    return Database::get().execute_read([&](auto& txn) {
        auto u =
            txn.exec_params("SELECT unit FROM habits WHERE owner_id = $1::uuid AND id = $2::uuid", owner, r.habit_id);
        return u.empty() ? std::string() : u[0][0].template as<std::string>();
    });
}

/// The values of a series by local day, or why there are none.
struct Series {
    ::Habits::Values values;
    Why why = Why::Ok;
};

namespace detail {

/// The metric of an entry: "steps" of "fitness:steps", "expense" of "money:expense:{currency}". The SQL
/// below is chosen by it from fixed text, never built from caller text.
inline std::string_view metric_of(const ::Metrics::Def& d) {
    const auto colon = d.id.find(':');
    const auto rest = d.id.substr(colon + 1);
    return rest.substr(0, rest.find(':'));
}

// The day of a timestamptz column in the zone $4, and the padded UTC window
// [$2 - 1, $3 + 2) that holds every instant of the local days $2..$3.
inline std::string local_day(std::string_view col) {
    return "(" + std::string(col) + " AT TIME ZONE $4)::date";
}
inline std::string padded(std::string_view col) {
    return std::string(col) + " >= ($2::date - 1)::timestamp AT TIME ZONE 'UTC' AND " + std::string(col) +
           " < ($3::date + 2)::timestamp AT TIME ZONE 'UTC'";
}

/// SELECT day, value over the timestamptz @p col of @p table, reduced by @p agg, the
/// rows filtered by @p where (beyond the account and the window).
inline std::string mi_agg(std::string_view table, std::string_view col, std::string_view agg, std::string_view where) {
    return "SELECT d::text, v::float8 FROM (SELECT " + local_day(col) + " AS d, " + std::string(agg) + " AS v FROM " +
           std::string(table) + " WHERE user_id = $1 AND $1 <> '' AND " + padded(col) + std::string(where) +
           " GROUP BY 1) t WHERE d BETWEEN $2::date AND $3::date AND v IS NOT NULL ORDER BY d";
}

/// SELECT day, value: the last row of each local day with @p expr not null.
inline std::string mi_last(std::string_view table,
                           std::string_view col,
                           std::string_view expr,
                           std::string_view where) {
    return "SELECT d::text, v::float8 FROM (SELECT DISTINCT ON (1) " + local_day(col) + " AS d, " + std::string(expr) +
           " AS v FROM " + std::string(table) + " WHERE user_id = $1 AND $1 <> '' AND " + padded(col) + " AND " +
           std::string(expr) + " IS NOT NULL" + std::string(where) + " ORDER BY 1, " + std::string(col) +
           " DESC) t WHERE d BETWEEN $2::date AND $3::date ORDER BY d";
}

/// The statement of a Mi entry: $1 the Xiaomi id, $2..$3 the days, $4 the zone.
inline std::string mi_sql(std::string_view m) {
    // Daily activity: the row's date, summed over the devices (the zone is not used).
    for (const auto& [name, col] : {std::pair<std::string_view, std::string_view>{"steps", "steps"},
                                    {"distance", "distance_m"},
                                    {"active_kcal", "active_kcal"},
                                    {"total_kcal", "total_kcal"},
                                    {"floors", "floors"},
                                    {"active_minutes", "active_minutes"}}) {
        if (m == name) {
            return "SELECT date::text, SUM(" + std::string(col) +
                   ")::float8 FROM daily_activity WHERE user_id = $1 AND $1 <> '' "
                   "AND date BETWEEN $2::date AND $3::date AND $4::text IS NOT NULL GROUP BY date HAVING COUNT(" +
                   std::string(col) + ") > 0 ORDER BY date";
        }
    }
    // The night: no nap, by its local end day, the latest ending one.
    for (const auto& [name, expr] :
         {std::pair<std::string_view, std::string_view>{"sleep", "time_asleep_minutes / 60.0"},
          {"sleep_duration", "duration_minutes / 60.0"},
          {"sleep_awake", "time_awake_minutes"},
          {"sleep_score", "sleep_score"}}) {
        if (m == name) {
            return "SELECT d::text, v::float8 FROM (SELECT DISTINCT ON (1) " + local_day("end_at") + " AS d, " +
                   std::string(expr) + " AS v FROM sleep_sessions WHERE user_id = $1 AND $1 <> '' AND NOT is_nap AND " +
                   padded("end_at") +
                   " ORDER BY 1, end_at DESC) t WHERE d BETWEEN $2::date AND $3::date AND v IS NOT NULL ORDER BY d";
        }
    }
    for (const auto& [name, col] : {std::pair<std::string_view, std::string_view>{"weight", "weight_kg"},
                                    {"bmi", "bmi"},
                                    {"body_fat", "body_fat_pct"},
                                    {"muscle", "muscle_mass_kg"},
                                    {"water", "water_pct"},
                                    {"bone", "bone_mass_kg"},
                                    {"visceral_fat", "visceral_fat_score"},
                                    {"bmr", "basal_metabolism_kcal"},
                                    {"metabolic_age", "metabolic_age"}}) {
        if (m == name) {
            return mi_last("body_measurements", "timestamp", col, "");
        }
    }
    if (m == "resting_hr") {
        return mi_last("heart_rate_samples", "timestamp", "bpm", " AND sample_type = 'resting'");
    }
    if (m == "hr_avg") {
        return mi_agg("heart_rate_samples", "timestamp", "AVG(bpm)", " AND sample_type <> 'resting'");
    }
    if (m == "hr_max") {
        return mi_agg("heart_rate_samples", "timestamp", "MAX(bpm)", " AND sample_type <> 'resting'");
    }
    if (m == "spo2") {
        return mi_agg("spo2_samples", "timestamp", "AVG(spo2_pct)", "");
    }
    if (m == "spo2_min") {
        return mi_agg("spo2_samples", "timestamp", "MIN(spo2_pct)", "");
    }
    if (m == "stress") {
        return mi_agg("stress_samples", "timestamp", "AVG(stress_score)", "");
    }
    if (m == "stress_max") {
        return mi_agg("stress_samples", "timestamp", "MAX(stress_score)", "");
    }
    if (m == "abnormal_hr") {
        return mi_agg("abnormal_heart_beat_events", "start_at", "COUNT(*)", "");
    }
    if (m == "band_workouts") {
        return mi_agg("workouts", "start_at", "COUNT(*)", "");
    }
    if (m == "band_workout_minutes") {
        return mi_agg("workouts", "start_at", "SUM(duration_minutes)", "");
    }
    if (m == "band_workout_kcal") {
        return mi_agg("workouts", "start_at", "SUM(calories_kcal)", "");
    }
    if (m == "band_workout_distance") {
        return mi_agg("workouts", "start_at", "SUM(distance_m)", "");
    }
    return "";
}

/// The statement of a food entry: $1 the owner, $2..$3 the days.
inline std::string food_sql(std::string_view m) {
    for (const auto& [name, col] : {std::pair<std::string_view, std::string_view>{"kcal", "kcal"},
                                    {"protein", "protein_g"},
                                    {"fat", "fat_g"},
                                    {"carbs", "carbs_g"},
                                    {"fiber", "fiber_g"},
                                    {"sugar", "sugar_g"},
                                    {"salt", "salt_g"}}) {
        if (m == name) {
            // SUM skips the entries without the number; a day where none has it has no value.
            return "SELECT date::text, SUM(" + std::string(col) +
                   ")::float8 FROM food_entries WHERE owner_id = $1::uuid "
                   "AND date BETWEEN $2::date AND $3::date GROUP BY date HAVING COUNT(" +
                   std::string(col) + ") > 0 ORDER BY date";
        }
    }
    return "";
}

/// The statement of a tape entry: $1 the owner, $2..$3 the days; the day's last
/// measurement that has the size.
inline std::string tape_sql(std::string_view m) {
    for (const auto& [name, col] : {std::pair<std::string_view, std::string_view>{"waist", "waist_cm"},
                                    {"chest", "chest_cm"},
                                    {"hips", "hips_cm"},
                                    {"neck", "neck_cm"},
                                    {"arm", "arm_cm"},
                                    {"thigh", "thigh_cm"}}) {
        if (m == name) {
            return "SELECT DISTINCT ON (date) date::text, " + std::string(col) +
                   "::float8 FROM tape_measurements WHERE owner_id = $1::uuid "
                   "AND date BETWEEN $2::date AND $3::date AND " +
                   std::string(col) + " IS NOT NULL ORDER BY date, created_at DESC, id DESC";
        }
    }
    return "";
}

/// The statement of a Training entry: $1 the owner, $2..$3 the days, $4 the zone;
/// the finished sessions by their local end day.
inline std::string workout_sql(std::string_view m) {
    std::string value;
    if (m == "sessions") {
        value = "COUNT(*)";
    } else if (m == "minutes") {
        value = "SUM(EXTRACT(EPOCH FROM (s.finished_at - s.started_at)) / 60.0)";
    } else if (m == "volume") {
        // Volume as SessionRepository counts it: reps x load over work sets; a
        // bodyweight exercise adds the session's body weight to the set's extra load.
        value =
            "SUM((SELECT COALESCE(SUM(ws.reps * (COALESCE(ws.weight_kg, 0) + "
            "  CASE WHEN e.tracking_mode = 'bodyweight_reps' THEN COALESCE(s.bodyweight_kg, 0) ELSE 0 END)), 0) "
            " FROM workout_sets ws JOIN session_exercises se ON se.id = ws.session_exercise_id "
            " JOIN exercises e ON e.id = se.exercise_id "
            " WHERE se.session_id = s.id AND ws.kind = 'work' AND ws.reps IS NOT NULL))";
    } else if (m == "sets") {
        value =
            "SUM((SELECT COUNT(*) FROM workout_sets ws JOIN session_exercises se ON se.id = ws.session_exercise_id "
            " WHERE se.session_id = s.id AND ws.kind = 'work'))";
    } else {
        return "";
    }
    return "SELECT d::text, v::float8 FROM (SELECT " + local_day("s.finished_at") + " AS d, " + value +
           " AS v FROM workout_sessions s WHERE s.owner_id = $1::uuid AND s.finished_at IS NOT NULL AND " +
           padded("s.finished_at") + " GROUP BY 1) t WHERE d BETWEEN $2::date AND $3::date ORDER BY d";
}

/// The statement of a LeetCode entry: $1 the owner, $2..$3 the days; a
/// day's value is its snapshot over the previous one (the last before it).
inline std::string leetcode_sql(std::string_view m) {
    std::string col;
    if (m == "solved") {
        col = "total";
    } else if (m == "easy" || m == "medium" || m == "hard") {
        col = std::string(m);
    } else {
        return "";
    }
    return "SELECT day::text, v::float8 FROM (SELECT day, " + col + " - LAG(" + col +
           ") OVER (ORDER BY day) AS v FROM leetcode_snapshots WHERE owner_id = $1::uuid AND day <= $3::date AND "
           "day >= COALESCE((SELECT max(day) FROM leetcode_snapshots WHERE owner_id = $1::uuid AND day < $2::date), "
           "$2::date)) t WHERE day BETWEEN $2::date AND $3::date AND v IS NOT NULL ORDER BY day";
}

template <typename Row>
void put(::Habits::Values& out, const Row& row) {
    out[::Habits::day_of(row[0].template as<std::string>())] =
        row[1].is_null() ? ::Habits::kNoNumber : row[1].template as<double>();
}

}  // namespace detail

/**
 * The series @p r of @p owner over the local days @p from..@p to (inclusive) in the
 * zone @p tz (an IANA name already checked). The caller bounds the range (the API
 * takes at most 400 days). When available() is not Ok the values are empty and
 * why says so. A day without rows has no value; a day whose row has no number (a
 * manual tick without a value) holds ::Habits::kNoNumber.
 */
inline Series read(const std::string& owner,
                   const ::Metrics::Ref& r,
                   ::Habits::Day from,
                   ::Habits::Day to,
                   const std::string& tz,
                   const Access& a) {
    Series out;
    out.why = available(owner, r, a);
    if (out.why != Why::Ok || to < from) {
        return out;
    }
    using ::Metrics::Gate;
    const std::string lo = ::Habits::text_of(from);
    const std::string hi = ::Habits::text_of(to);
    const std::string_view m = detail::metric_of(*r.def);

    if (r.def->gate == Gate::Money && m == "balance") {
        // The end of each day, the accounts of the currency summed, as the money summary counts it.
        for (const auto& [day, by_currency] : Repositories::Money::SummaryRepository().daily_balances(owner, lo, hi)) {
            const auto it = by_currency.find(r.currency);
            if (it != by_currency.end()) {
                out.values[::Habits::day_of(day)] = it->second;
            }
        }
        return out;
    }

    Database::get().execute_read([&](auto& txn) {
        switch (r.def->gate) {
            case Gate::Mi:
                for (const auto& row : txn.exec_params(detail::mi_sql(m), a.xiaomi_id, lo, hi, tz)) {
                    detail::put(out.values, row);
                }
                break;
            case Gate::Food:
                for (const auto& row : txn.exec_params(detail::food_sql(m), owner, lo, hi)) {
                    detail::put(out.values, row);
                }
                break;
            case Gate::Tape:
                for (const auto& row : txn.exec_params(detail::tape_sql(m), owner, lo, hi)) {
                    detail::put(out.values, row);
                }
                break;
            case Gate::Workout:
                for (const auto& row : txn.exec_params(detail::workout_sql(m), owner, lo, hi, tz)) {
                    detail::put(out.values, row);
                }
                break;
            case Gate::LeetCode:
                for (const auto& row : txn.exec_params(detail::leetcode_sql(m), owner, lo, hi)) {
                    detail::put(out.values, row);
                }
                break;
            case Gate::Habits:
                if (r.def->param == ::Metrics::Param::Habit) {
                    for (const auto& row :
                         txn.exec_params("SELECT c.date::text, c.value::float8 FROM habit_checks c JOIN habits h ON "
                                         "h.id = c.habit_id "
                                         "WHERE h.owner_id = $1::uuid AND h.id = $4::uuid AND h.source = 'manual' "
                                         "AND c.date BETWEEN $2::date AND $3::date ORDER BY c.date",
                                         owner,
                                         lo,
                                         hi,
                                         r.habit_id)) {
                        detail::put(out.values, row);
                    }
                } else {
                    for (const auto& row : txn.exec_params(
                             "SELECT date::text, score::float8 FROM mood_checkins WHERE owner_id = $1::uuid "
                             "AND date BETWEEN $2::date AND $3::date ORDER BY date",
                             owner,
                             lo,
                             hi)) {
                        detail::put(out.values, row);
                    }
                }
                break;
            case Gate::Tasks:
                for (const auto& row : txn.exec_params(
                         "SELECT d::text, n::float8 FROM (SELECT " + detail::local_day("completed_at") +
                             " AS d, COUNT(*) AS n FROM task_items WHERE owner_id = $1::uuid AND status = 'done' AND " +
                             detail::padded("completed_at") +
                             " GROUP BY 1) t WHERE d BETWEEN $2::date AND $3::date ORDER BY d",
                         owner,
                         lo,
                         hi,
                         tz)) {
                    detail::put(out.values, row);
                }
                break;
            case Gate::Money: {
                // Posted rows in the currency's accounts; an expense counts the bank's
                // adjustments too, as the money reports do, and an adjustment counts in the
                // category of the expense it adjusts (it has none of its own; report_rows in
                // money/TransactionRepository.hpp). $5 is the category or ''.
                const bool income = m == "income";
                for (const auto& row :
                     txn.exec_params("SELECT t.date::text, SUM(t.amount)::float8 FROM money_transactions t "
                                     "JOIN money_accounts a ON a.id = t.account_id "
                                     "LEFT JOIN money_transactions o ON o.id = t.adjusts_id "
                                     "WHERE t.owner_id = $1::uuid AND a.owner_id = $1::uuid AND a.currency = $4 "
                                     "AND t.status = 'posted' AND (t.type = 'income') = $6::boolean "
                                     "AND ($5 = '' OR CASE WHEN t.type <> 'fx_adjustment' THEN t.category_id "
                                     "WHEN o.type = 'expense' THEN o.category_id END = NULLIF($5, '')::uuid) "
                                     "AND t.date BETWEEN $2::date AND $3::date GROUP BY t.date ORDER BY t.date",
                                     owner,
                                     lo,
                                     hi,
                                     r.currency,
                                     r.category_id,
                                     income)) {
                    detail::put(out.values, row);
                }
                break;
            }
            case Gate::Practice:
                for (const auto& row : txn.exec_params(
                         "SELECT date::text, SUM(amount)::float8 FROM practice_entries WHERE owner_id = $1::uuid "
                         "AND kind = $4 AND currency = $5 AND date BETWEEN $2::date AND $3::date "
                         "GROUP BY date ORDER BY date",
                         owner,
                         lo,
                         hi,
                         std::string(m),
                         r.currency)) {
                    detail::put(out.values, row);
                }
                break;
        }
        return true;
    });
    return out;
}

/// What the parametrised entries expand to for @p owner: the currencies of the
/// money accounts, the currencies of the practice (its entries and the money
/// accounts), the expense categories, the active habits ticked by hand.
struct Choices {
    struct Named {
        std::string id;
        std::string name;
        std::string unit;
    };
    std::vector<std::string> money_currencies;
    std::vector<std::string> practice_currencies;
    std::vector<Named> categories;
    std::vector<Named> habits;
};

inline Choices choices_of(const std::string& owner) {
    return Database::get().execute_read([&](auto& txn) {
        Choices c;
        for (const auto& row : txn.exec_params(
                 "SELECT DISTINCT currency FROM money_accounts WHERE owner_id = $1::uuid ORDER BY 1", owner)) {
            c.money_currencies.push_back(row[0].template as<std::string>());
        }
        for (const auto& row :
             txn.exec_params("SELECT currency FROM money_accounts WHERE owner_id = $1::uuid "
                             "UNION SELECT currency FROM practice_entries WHERE owner_id = $1::uuid ORDER BY 1",
                             owner)) {
            c.practice_currencies.push_back(row[0].template as<std::string>());
        }
        for (const auto& row : txn.exec_params(
                 "SELECT id::text, name FROM money_categories WHERE owner_id = $1::uuid AND kind = 'expense' "
                 "AND NOT archived ORDER BY position, name",
                 owner)) {
            c.categories.push_back({row[0].template as<std::string>(), row[1].template as<std::string>(), ""});
        }
        for (const auto& row :
             txn.exec_params("SELECT id::text, name, unit FROM habits WHERE owner_id = $1::uuid AND source = 'manual' "
                             "AND archived_at IS NULL ORDER BY position, created_at",
                             owner)) {
            c.habits.push_back({row[0].template as<std::string>(),
                                row[1].template as<std::string>(),
                                row[2].template as<std::string>()});
        }
        return c;
    });
}

}  // namespace Repositories::Metrics
