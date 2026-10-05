/**
 * @file SyncService.cpp
 * @brief Sync orchestration bodies.
 */

#include "fitness/sync/SyncService.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <utility>

#include <spdlog/spdlog.h>

#include "database/Database.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Normalize.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "repositories/fitness/ActivityRepository.hpp"
#include "repositories/fitness/BodyRepository.hpp"
#include "repositories/fitness/SamplesRepository.hpp"
#include "repositories/fitness/SleepRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "repositories/fitness/WorkoutRepository.hpp"
#include "utils/Config.hpp"

namespace Sync {

namespace {

struct Chunk {
    std::string from;
    std::string to;
};

/// Range split into chunk_days pieces, inclusive on both ends.
std::vector<Chunk> split_range(const std::string& from, const std::string& to, int chunk_days) {
    using namespace std::chrono;
    const sys_days start = Xiaomi::detail::parse_date(from);
    const sys_days end = Xiaomi::detail::parse_date(to);
    std::vector<Chunk> chunks;
    for (sys_days cursor = start; cursor <= end;) {
        sys_days chunk_end = cursor + days{chunk_days - 1};
        if (chunk_end > end) {
            chunk_end = end;
        }
        const auto iso = [](sys_days d) {
            const year_month_day ymd{d};
            char out[16];
            std::snprintf(out,
                          sizeof(out),
                          "%04d-%02d-%02d",
                          static_cast<int>(ymd.year()),
                          static_cast<unsigned>(ymd.month()),
                          static_cast<unsigned>(ymd.day()));
            return std::string(out);
        };
        chunks.push_back({iso(cursor), iso(chunk_end)});
        cursor = chunk_end + days{1};
    }
    return chunks;
}

/// Atomic queued -> running transition. false when another run is already in progress.
///
/// Before claiming, stale running rows are marked interrupted: a worker killed
/// mid-sync (OOM, node eviction) never clears its own row, and without this
/// step the partial index one_running_sync_run would block every future run
/// forever (Critical 2 of the final review). The stale threshold exceeds the
/// longest honest duration: the per-type ceiling times the number of types,
/// plus a margin.
bool try_start(long run_id, long stale_seconds) {
    try {
        return Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE sync_runs SET status = 'interrupted', finished_at = now() "
                "WHERE status = 'running' AND started_at < now() - make_interval(secs => $1::double precision)",
                stale_seconds);
            // Only from queued: a redelivered job must not revive a closed run
            // or overwrite its journal (Important 4).
            auto r = txn.exec_params(
                "UPDATE sync_runs SET status = 'running', started_at = now() "
                "WHERE id = $1 AND status = 'queued' RETURNING id",
                run_id);
            return !r.empty();
        });
    } catch (const std::exception&) {
        // Violation of the partial unique index one_running_sync_run.
        return false;
    }
}

/// YYYY-MM-DD date shifted by days. The caller has already validated the format.
std::string shift_date(const std::string& date, int days) {
    std::chrono::year_month_day ymd{std::chrono::year(std::stoi(date.substr(0, 4))),
                                    std::chrono::month(static_cast<unsigned>(std::stoi(date.substr(5, 2)))),
                                    std::chrono::day(static_cast<unsigned>(std::stoi(date.substr(8, 2))))};
    const auto shifted = std::chrono::year_month_day(std::chrono::sys_days(ymd) + std::chrono::days(days));
    char out[16];
    std::snprintf(out,
                  sizeof(out),
                  "%04d-%02u-%02u",
                  static_cast<int>(shifted.year()),
                  static_cast<unsigned>(shifted.month()),
                  static_cast<unsigned>(shifted.day()));
    return out;
}

void bump_sync_state(const std::string& data_type, long added) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "INSERT INTO sync_state (data_type, last_sync_at, records_count) "
            "VALUES ($1, now(), $2) "
            "ON CONFLICT (data_type) DO UPDATE SET "
            "last_sync_at = now(), records_count = sync_state.records_count + $2",
            data_type,
            added);
        return true;
    });
}

}  // namespace

SyncService::SyncService(Xiaomi::HttpTransport& transport,
                         Xiaomi::Credentials credentials,
                         std::function<void(const Xiaomi::Credentials&)> on_rotate)
    : transport_(transport), credentials_(std::move(credentials)), on_rotate_(std::move(on_rotate)) {}

nlohmann::json SyncService::run(long run_id,
                                const std::string& from,
                                const std::string& to,
                                const std::vector<std::string>& data_types) {
    Repositories::SyncRunRepository runs;
    nlohmann::json result = nlohmann::json::object();

    int chunk_days = 7;
    long type_timeout = 180;
    if (Config::is_initialized()) {
        chunk_days = Config::get().get<int>("fitness.xiaomi.sync_chunk_days", "MI_FITNESS_CHUNK_DAYS", 7);
        if (chunk_days < 1) {
            // Zero in the knob hung split_range forever (minor of plan review 2).
            chunk_days = 1;
        }
        type_timeout =
            Config::get().get<long>("fitness.xiaomi.sync_type_timeout_seconds", "MI_FITNESS_SYNC_TYPE_TIMEOUT", 180);
    }

    const long stale_seconds = type_timeout * static_cast<long>(kAllDataTypes.size()) + 600;
    if (!try_start(run_id, stale_seconds)) {
        // Another run holds running: honest skipped, the cloud is not touched.
        // Only a queued run gets its journal updated: a redelivered job must
        // not overwrite a finished run.
        result["skipped_reason"] = "another sync run is in progress";
        runs.skip_if_queued(run_id, result);
        return result;
    }

    Xiaomi::CloudClient client(transport_, credentials_, on_rotate_);
    login_outcome_ = Login::NotAttempted;
    bool logged_in = false;
    bool auth_dead = false;
    bool any_failed = false;

    for (const auto& data_type : data_types) {
        auto& entry = result[data_type];
        const auto type_started = std::chrono::steady_clock::now();
        // An auth failure is fixed by a fresh token, not by retrying: after
        // the first auth failure the remaining types are closed without
        // touching the network, otherwise one run burns up to eight logins
        // with a dead token (Important 5 of the final review).
        if (auth_dead) {
            any_failed = true;
            entry["error"] = "auth";
            continue;
        }
        try {
            if (!logged_in) {
                client.login();
                logged_in = true;
                login_outcome_ = Login::Accepted;
            }
            Repositories::UpsertCounts counts;
            long skipped = 0;
            long suppressed = 0;
            // daily_activity is aggregated over the whole range: chunk bounds
            // are in the region's zone, the device may live in another, and a
            // day on a chunk boundary is spread across two fetches. Per-chunk
            // upsert overwrote the full aggregate with a fragment (bridge
            // parity check, 2026-09 report), so minutes are collected here and
            // normalized once after all chunks.
            std::vector<nlohmann::json> activity_steps;
            std::vector<nlohmann::json> activity_calories;

            for (const auto& chunk : split_range(from, to, chunk_days)) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - type_started)
                        .count();
                if (elapsed > type_timeout) {
                    // Per-type ceiling, checked between chunks: a request cannot be
                    // safely interrupted midway, and each is bounded by the transport timeout.
                    throw Xiaomi::MiFitnessProtocolError("type exceeded its time budget");
                }
                const std::string& user = client.credentials().user_id;
                if (data_type == "daily_activity") {
                    const auto steps = client.fetch_key("steps", chunk.from, chunk.to, {});
                    const auto calories = client.fetch_key("calories", chunk.from, chunk.to, {});
                    activity_steps.insert(activity_steps.end(), steps.begin(), steps.end());
                    activity_calories.insert(activity_calories.end(), calories.begin(), calories.end());
                } else if (data_type == "sleep") {
                    const auto records = client.fetch_key("sleep", chunk.from, chunk.to, {});
                    auto sessions = Xiaomi::normalize_sleep(records, user, skipped);
                    // Report window: wake-up dates of candidates without a score,
                    // with a day of margin for zone differences.
                    std::string report_from;
                    std::string report_to;
                    for (const auto& s : sessions) {
                        if (s.sleep_score.has_value() || s.is_nap) {
                            continue;
                        }
                        const std::string day = s.end_at.substr(0, 10);
                        if (report_from.empty() || day < report_from) {
                            report_from = day;
                        }
                        if (report_to.empty() || day > report_to) {
                            report_to = day;
                        }
                    }
                    if (!report_from.empty()) {
                        try {
                            // A day of margin on both sides, like the reference: the
                            // report's zone may differ from the region's, and the
                            // edge day's report falls outside a narrow window.
                            const auto reports =
                                client.fetch_daily_sleep_reports(shift_date(report_from, -1), shift_date(report_to, 1));
                            const int offset =
                                credentials_.region.empty() || credentials_.region == "cn" ? 8 * 3600 : 0;
                            Xiaomi::apply_daily_sleep_scores(sessions, reports, offset);
                        } catch (const std::exception&) {
                            // Reports are optional: their failure does not affect sessions.
                            spdlog::warn("sleep: daily score lookup unavailable");
                        }
                    }
                    const auto c = Repositories::SleepRepository().upsert(sessions);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else if (data_type == "workouts") {
                    const auto records = client.fetch_sport_records(chunk.from, chunk.to);
                    const auto items = Xiaomi::normalize_workouts(records, user, skipped);
                    const auto c = Repositories::WorkoutRepository().upsert(items);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else if (data_type == "body_measurements") {
                    const auto records = client.fetch_key("weight", chunk.from, chunk.to, {});
                    const auto items = Xiaomi::normalize_body(records, user, skipped);
                    const auto c = Repositories::BodyRepository().upsert(items);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else if (data_type == "heart_rate") {
                    const auto records = client.fetch_key("heart_rate", chunk.from, chunk.to, {});
                    const auto resting = client.fetch_key("resting_heart_rate", chunk.from, chunk.to, {});
                    const auto items = Xiaomi::normalize_heart_rate(records, resting, user, skipped);
                    const auto c = Repositories::SamplesRepository().upsert_heart_rate(items);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else if (data_type == "spo2") {
                    const auto records = client.fetch_key("spo2", chunk.from, chunk.to, {});
                    const auto items = Xiaomi::normalize_spo2(records, user, skipped);
                    const auto c = Repositories::SamplesRepository().upsert_spo2(items);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else if (data_type == "stress") {
                    const auto records = client.fetch_key("stress", chunk.from, chunk.to, {});
                    const auto items = Xiaomi::normalize_stress(records, user, skipped);
                    const auto c = Repositories::SamplesRepository().upsert_stress(items);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else if (data_type == "abnormal_heart_beat") {
                    const auto records = client.fetch_key("abnormal_heart_beat", chunk.from, chunk.to, {});
                    const auto items = Xiaomi::normalize_abnormal_heart_beat(records, user, skipped);
                    const auto c = Repositories::SamplesRepository().upsert_abnormal(items);
                    counts.added += c.added;
                    counts.updated += c.updated;
                } else {
                    throw Xiaomi::MiFitnessProtocolError("unknown data type requested");
                }
            }

            if (data_type == "daily_activity") {
                const std::string& user = client.credentials().user_id;
                auto normalized = Xiaomi::normalize_daily_activity(activity_steps, activity_calories, user);
                // The window is cut in the region's zone, so minutes of adjacent
                // local days fall inside it. Days outside [from, to] are partial
                // by construction: upserting them would overwrite a full day in
                // the database with a fragment (Critical 1 of the final review).
                std::erase_if(normalized.days, [&](const auto& day) { return day.date < from || day.date > to; });
                const auto c = Repositories::ActivityRepository().upsert(normalized.days);
                counts.added += c.added;
                counts.updated += c.updated;
                skipped += normalized.skipped;
                suppressed += normalized.suppressed_steps;
            }

            entry["added"] = counts.added;
            entry["updated"] = counts.updated;
            entry["skipped"] = skipped;
            if (suppressed > 0) {
                entry["suppressed_steps"] = suppressed;
            }
            bump_sync_state(data_type, counts.added);
        } catch (const Xiaomi::MiFitnessAuthError& e) {
            any_failed = true;
            auth_dead = !logged_in;
            if (auth_dead) {
                login_outcome_ = Login::Refused;
            }
            entry["error"] = "auth";
            spdlog::warn("sync {}: auth failure: {}", data_type, e.what());
        } catch (const Xiaomi::MiFitnessProtocolError& e) {
            any_failed = true;
            entry["error"] = "protocol";
            spdlog::warn("sync {}: protocol failure: {}", data_type, e.what());
        } catch (const std::exception& e) {
            any_failed = true;
            entry["error"] = "other";
            spdlog::warn("sync {}: failure: {}", data_type, e.what());
        }
    }

    runs.finish(run_id, any_failed ? "failed" : "succeeded", result);
    return result;
}

}  // namespace Sync
