/**
 * @file Normalize.hpp
 * @brief Raw cloud records -> domain structures. Pure functions.
 *
 * No network, no database: the input is JSON records of the form {time,
 * zone_offset, zone_name, sid, value}, the output is domain structures. A
 * broken record increments skipped and never fails the whole type. Semantics
 * mirror the reference Python adapter verbatim, any deviation is a defect.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "domain/fitness/Health.hpp"

namespace Xiaomi {

/// Timestamps before year 2000 are device garbage, not data.
inline constexpr std::int64_t kMinValidTimestamp = 946684800;

struct ActivityResult {
    std::vector<Domain::DailyActivity> days;
    long suppressed_steps = 0;
    long skipped = 0;
};

/**
 * @brief Daily activity from per-minute step slices and the separate calories key.
 *
 * Grouped by the record's local minute. On a minute collision (phone and band
 * send parallel slices of the same activity) the record with the larger tuple
 * (steps, distance, calories) wins: a sum would double-count, the observed
 * overshoot in the reference was +73..+208 steps per day. Suppressed steps are
 * counted. Calories from the calories key replace the step-derived ones where present.
 */
ActivityResult normalize_daily_activity(const std::vector<nlohmann::json>& step_records,
                                        const std::vector<nlohmann::json>& calorie_records,
                                        std::string_view user_id);

/**
 * @brief Sleep sessions from raw records of the sleep key.
 *
 * Field priorities, stage mapping and score validity rules are taken from the
 * reference verbatim. A record without both boundaries is skipped. skipped
 * grows on broken records and never fails the whole type.
 */
std::vector<Domain::SleepSession> normalize_sleep(const std::vector<nlohmann::json>& records,
                                                  std::string_view user_id,
                                                  long& skipped);

/**
 * @brief Apply daily reports to sessions: one unambiguous main session.
 *
 * The score is never invented or smeared: the report's segment boundaries are
 * authoritative, without them a choice is possible only within a single source
 * and a single longest session, conflicting scores leave NULL, the record's
 * own score is never overwritten. default_zone_offset is substituted for
 * reports without zone_offset (region cn is 28800).
 */
void apply_daily_sleep_scores(std::vector<Domain::SleepSession>& sessions,
                              const std::vector<nlohmann::json>& reports,
                              int default_zone_offset);

/// Workouts from the separate endpoint's records. A missing end_time is
/// start_time + duration. All metrics follow "zero is NULL".
std::vector<Domain::Workout> normalize_workouts(const std::vector<nlohmann::json>& records,
                                                std::string_view user_id,
                                                long& skipped);

/// Weight and body composition. A record without weight is skipped entirely: not a measurement.
std::vector<Domain::BodyMeasurement> normalize_body(const std::vector<nlohmann::json>& records,
                                                    std::string_view user_id,
                                                    long& skipped);

/// Heart rate: regular records (type 0 passive, otherwise active) plus the
/// separate resting key with sample_type resting and time from date_time | time.
std::vector<Domain::HeartRateSample> normalize_heart_rate(const std::vector<nlohmann::json>& records,
                                                          const std::vector<nlohmann::json>& resting_records,
                                                          std::string_view user_id,
                                                          long& skipped);

/// SpO2 from spo2 | value; a record without a value is skipped.
std::vector<Domain::Spo2Sample> normalize_spo2(const std::vector<nlohmann::json>& records,
                                               std::string_view user_id,
                                               long& skipped);

/// Stress from stress | score | value; level <30 low, <60 medium, otherwise high.
std::vector<Domain::StressSample> normalize_stress(const std::vector<nlohmann::json>& records,
                                                   std::string_view user_id,
                                                   long& skipped);

/// Rhythm anomalies; a missing end_time equals start_time.
std::vector<Domain::AbnormalHeartBeatEvent> normalize_abnormal_heart_beat(const std::vector<nlohmann::json>& records,
                                                                          std::string_view user_id,
                                                                          long& skipped);

namespace detail {

/// The record's local epoch seconds: time + zone_offset. Throws on a broken or
/// prehistoric time.
std::int64_t record_epoch(const nlohmann::json& item);

/// value arrives both as a string with JSON inside and as a ready object.
nlohmann::json parse_value(const nlohmann::json& item);

/// ISO string with an explicit offset: 2026-09-22T10:00:00+08:00.
std::string iso_with_offset(std::int64_t epoch, int offset_seconds);

}  // namespace detail

}  // namespace Xiaomi
