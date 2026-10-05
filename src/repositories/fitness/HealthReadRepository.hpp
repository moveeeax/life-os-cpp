/**
 * @file HealthReadRepository.hpp
 * @brief Health data reads for the data read routes.
 *
 * Postgres builds the JSON (json_agg over a subquery): rows are not pushed
 * through C++ one by one, the repository parses a single cell. Times go out
 * as ISO UTC with a +00:00 suffix; range bounds are UTC days, the comparison
 * is on timestamptz without a cast to date so that the migration 016 indexes
 * are used.
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories {

class HealthReadRepository {
public:
    /**
     * @param xiaomi_user_id the account whose rows are read: the fitness tables
     *        are keyed by the Xiaomi id, and every query here filters by it.
     *        An empty id (a user without a linked account) matches no row.
     */
    explicit HealthReadRepository(std::string xiaomi_user_id) : account_(std::move(xiaomi_user_id)) {}

    struct Page {
        nlohmann::json rows = nlohmann::json::array();
        long total = 0;
    };

    Page daily_activity(const std::string& from, const std::string& to, long limit, long offset) {
        return page(
            "SELECT date::text, steps, distance_m, active_kcal, total_kcal, timezone "
            "FROM daily_activity WHERE user_id = {ACC} AND date BETWEEN $1 AND $2 ORDER BY date",
            "SELECT COUNT(*) FROM daily_activity WHERE user_id = {ACC} AND date BETWEEN $1 AND $2",
            from,
            to,
            limit,
            offset);
    }

    Page sleep(const std::string& from, const std::string& to, long limit, long offset) {
        return page("SELECT sleep_id, " + iso("start_at") + " AS start_at, " + iso("end_at") +
                        " AS end_at, duration_minutes, time_asleep_minutes, time_awake_minutes, "
                        "sleep_score, sleep_score_source, is_nap, timezone, stages "
                        "FROM sleep_sessions WHERE " +
                        day_range("end_at") + " ORDER BY end_at",
                    "SELECT COUNT(*) FROM sleep_sessions WHERE " + day_range("end_at"),
                    from,
                    to,
                    limit,
                    offset);
    }

    Page heart_rate(const std::string& from, const std::string& to, const std::string& type, long limit, long offset) {
        // Empty type means all types; the filter is a parameter, not a
        // concatenated value. Placeholder numbers differ between the select
        // and the count: in the select $3/$4 are taken by limit/offset.
        return page("SELECT " + iso("timestamp") +
                        " AS timestamp, bpm, sample_type "
                        "FROM heart_rate_samples WHERE " +
                        day_range("timestamp") + " AND ($5 = '' OR sample_type = $5) ORDER BY timestamp",
                    "SELECT COUNT(*) FROM heart_rate_samples WHERE " + day_range("timestamp") +
                        " AND ($3 = '' OR sample_type = $3)",
                    from,
                    to,
                    limit,
                    offset,
                    type);
    }

    Page stress(const std::string& from, const std::string& to, long limit, long offset) {
        return page("SELECT " + iso("timestamp") +
                        " AS timestamp, stress_score, level "
                        "FROM stress_samples WHERE " +
                        day_range("timestamp") + " ORDER BY timestamp",
                    "SELECT COUNT(*) FROM stress_samples WHERE " + day_range("timestamp"),
                    from,
                    to,
                    limit,
                    offset);
    }

    Page spo2(const std::string& from, const std::string& to, long limit, long offset) {
        return page("SELECT " + iso("timestamp") +
                        " AS timestamp, spo2_pct "
                        "FROM spo2_samples WHERE " +
                        day_range("timestamp") + " ORDER BY timestamp",
                    "SELECT COUNT(*) FROM spo2_samples WHERE " + day_range("timestamp"),
                    from,
                    to,
                    limit,
                    offset);
    }

    Page body(const std::string& from, const std::string& to, long limit, long offset) {
        return page("SELECT " + iso("timestamp") +
                        " AS timestamp, weight_kg, bmi, body_fat_pct, muscle_mass_kg, "
                        "water_pct, bone_mass_kg, visceral_fat_score, basal_metabolism_kcal, "
                        "metabolic_age FROM body_measurements WHERE " +
                        day_range("timestamp") + " ORDER BY timestamp",
                    "SELECT COUNT(*) FROM body_measurements WHERE " + day_range("timestamp"),
                    from,
                    to,
                    limit,
                    offset);
    }

    Page workouts(const std::string& from, const std::string& to, long limit, long offset) {
        return page("SELECT workout_id, activity_type, " + iso("start_at") + " AS start_at, " + iso("end_at") +
                        " AS end_at, duration_minutes, distance_m, calories_kcal, "
                        "avg_heart_rate_bpm, max_heart_rate_bpm, avg_pace_sec_per_km, "
                        "max_pace_sec_per_km, total_steps FROM workouts WHERE " +
                        day_range("start_at") + " ORDER BY start_at",
                    "SELECT COUNT(*) FROM workouts WHERE " + day_range("start_at"),
                    from,
                    to,
                    limit,
                    offset);
    }

    Page summary(
        const std::string& from, const std::string& to, long limit, long offset, long long zone_offset_seconds) {
        // The activity date is local (device zone), so the sleep and resting
        // heart rate windows start at local midnight in the region's zone: a
        // UTC window pushed morning records into the previous row (phase 3
        // review, Important 5). The residual shift of the device zone against
        // the region zone leaves an hour-wide edge, recorded as a Ruling.
        const std::string day_start =
            "(a.date::timestamp AT TIME ZONE 'UTC' - make_interval(secs => $5::double precision))";
        const std::string select =
            "SELECT a.date::text, a.steps, a.distance_m, a.active_kcal, "
            "s.duration_minutes AS sleep_duration_minutes, s.sleep_score, hr.bpm AS resting_bpm "
            "FROM daily_activity a "
            "LEFT JOIN LATERAL (SELECT duration_minutes, sleep_score FROM sleep_sessions "
            " WHERE user_id = {ACC} AND NOT is_nap AND end_at >= " +
            day_start + " AND end_at < " + day_start +
            " + interval '1 day' ORDER BY duration_minutes DESC LIMIT 1) s ON true "
            "LEFT JOIN LATERAL (SELECT bpm FROM heart_rate_samples "
            " WHERE user_id = {ACC} AND sample_type = 'resting' AND timestamp >= " +
            day_start + " AND timestamp < " + day_start +
            " + interval '1 day' "
            " ORDER BY timestamp DESC LIMIT 1) hr ON true "
            "WHERE a.user_id = {ACC} AND a.date BETWEEN $1 AND $2 ORDER BY a.date";
        return page(select,
                    "SELECT COUNT(*) FROM daily_activity WHERE user_id = {ACC} AND date BETWEEN $1 AND $2 "
                    "AND $3::bigint > -86401",
                    from,
                    to,
                    limit,
                    offset,
                    zone_offset_seconds);
    }

    Page abnormal_heart_beat(const std::string& from, const std::string& to, long limit, long offset) {
        return page("SELECT event_id, " + iso("start_at") + " AS start_at, " + iso("end_at") +
                        " AS end_at, duration_seconds FROM abnormal_heart_beat_events WHERE " + day_range("start_at") +
                        " ORDER BY start_at",
                    "SELECT COUNT(*) FROM abnormal_heart_beat_events WHERE " + day_range("start_at"),
                    from,
                    to,
                    limit,
                    offset);
    }

    /// Bounds and counts per type plus the time of the last sync.
    nlohmann::json coverage() {
        nlohmann::json out = nlohmann::json::object();
        Database::get().execute_read([&](auto& txn) {
            const struct {
                const char* type;
                const char* table;
                const char* column;
            } kTables[] = {
                {"daily_activity", "daily_activity", "date"},
                {"sleep", "sleep_sessions", "end_at"},
                {"heart_rate", "heart_rate_samples", "timestamp"},
                {"stress", "stress_samples", "timestamp"},
                {"spo2", "spo2_samples", "timestamp"},
                {"body_measurements", "body_measurements", "timestamp"},
                {"workouts", "workouts", "start_at"},
                {"abnormal_heart_beat", "abnormal_heart_beat_events", "start_at"},
            };
            for (const auto& e : kTables) {
                const std::string col(e.column);
                // For timestamptz the date is taken in UTC, not the session zone.
                const std::string day = col == "date" ? col : "(" + col + " AT TIME ZONE 'UTC')";
                auto r = txn.exec_params(
                    "SELECT json_build_object("
                    "'first_date', MIN(" +
                        day +
                        ")::date::text, "
                        "'last_date', MAX(" +
                        day +
                        ")::date::text, "
                        "'records', COUNT(*)) FROM " +
                        std::string(e.table) + " WHERE user_id = $1",
                    account_);
                out[e.type] = nlohmann::json::parse(r[0][0].template as<std::string>());
            }
            auto s = txn.exec_params(
                "SELECT COALESCE(json_object_agg(data_type, to_char(last_sync_at AT TIME ZONE 'UTC', "
                "'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"')), '{}'::json) FROM sync_state WHERE xiaomi_user_id = $1",
                account_);
            const auto last = nlohmann::json::parse(s[0][0].template as<std::string>());
            for (auto& [type, entry] : out.items()) {
                entry["last_sync_at"] = last.contains(type) ? last[type] : nlohmann::json();
            }
            return 0;
        });
        return out;
    }

    /// All rows of a type over the range, unpaginated: the export page.
    nlohmann::json export_rows(const std::string& type, const std::string& from, const std::string& to) {
        const long kNoLimit = 100000000;
        if (type == "daily_activity")
            return daily_activity(from, to, kNoLimit, 0).rows;
        if (type == "sleep")
            return sleep(from, to, kNoLimit, 0).rows;
        if (type == "heart_rate")
            return heart_rate(from, to, "", kNoLimit, 0).rows;
        if (type == "stress")
            return stress(from, to, kNoLimit, 0).rows;
        if (type == "spo2")
            return spo2(from, to, kNoLimit, 0).rows;
        if (type == "body_measurements")
            return body(from, to, kNoLimit, 0).rows;
        if (type == "workouts")
            return workouts(from, to, kNoLimit, 0).rows;
        if (type == "abnormal_heart_beat")
            return abnormal_heart_beat(from, to, kNoLimit, 0).rows;
        return nlohmann::json::array();
    }

private:
    /// ISO UTC with an explicit +00:00: timestamptz goes out without losing meaning.
    static std::string iso(const std::string& column) {
        return "to_char(" + column + " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"')";
    }

    /// UTC days over timestamptz: a half-open interval, index friendly.
    /// Explicit AT TIME ZONE 'UTC': the date -> timestamptz cast takes
    /// midnight in the Postgres session zone, while the response promises UTC
    /// (phase 3 review, Important 4; production runs in Etc/UTC, the fix is defensive).
    /// The account filter rides along: every caller reads one table of the account.
    static std::string day_range(const std::string& column) {
        return "(user_id = {ACC} AND " + column + " >= $1::date::timestamp AT TIME ZONE 'UTC' AND " + column +
               " < ($2::date + 1)::timestamp AT TIME ZONE 'UTC')";
    }

    /// Replace the {ACC} marker with the placeholder of the account parameter.
    static std::string with_account(std::string sql, int index) {
        static constexpr std::string_view kMarker = "{ACC}";
        const std::string placeholder = "$" + std::to_string(index);
        for (auto at = sql.find(kMarker); at != std::string::npos; at = sql.find(kMarker, at)) {
            sql.replace(at, kMarker.size(), placeholder);
        }
        return sql;
    }

    template <typename... Extra>
    Page page(const std::string& select,
              const std::string& count_sql,
              const std::string& from,
              const std::string& to,
              long limit,
              long offset,
              Extra&&... extra) {
        // The account is the last parameter of both statements; its number
        // depends on how many extra parameters the query has.
        const int extras = static_cast<int>(sizeof...(Extra));
        const std::string select_sql = with_account(select, 5 + extras);
        const std::string total_sql = with_account(count_sql, 3 + extras);
        Page out;
        Database::get().execute_read([&](auto& txn) {
            auto agg = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (" + select_sql + " LIMIT $3 OFFSET $4) t",
                from,
                to,
                limit,
                offset,
                extra...,
                account_);
            out.rows = nlohmann::json::parse(agg[0][0].template as<std::string>());
            auto total = txn.exec_params(total_sql, from, to, extra..., account_);
            out.total = total[0][0].template as<long>();
            return 0;
        });
        return out;
    }

    std::string account_;
};

}  // namespace Repositories
