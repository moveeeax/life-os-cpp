/**
 * @file SyncService.cpp
 * @brief Sync orchestration bodies.
 */

#include "fitness/sync/SyncService.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <pqxx/pqxx>
#include <set>
#include <utility>

#include <spdlog/spdlog.h>

#include "database/Database.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Normalize.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "fitness/xiaomi/Service.hpp"
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

using Repositories::SyncRunRepository;

struct Claim {
    SyncService::Start outcome = SyncService::Start::Skipped;
    long long retry_in_ms = 0;
};

/// Atomic transition to running. Skipped when another run of the same account
/// is in progress or the run is closed; runs of other accounts do not matter.
///
/// Before claiming, quiet running rows of the account are marked interrupted:
/// a worker killed mid-sync (OOM, node eviction) never clears its own row, and
/// without this step the partial index one_running_sync_run_per_account would
/// block the account's runs (Critical 2 of the final review). The run's own row
/// is not interrupted but taken over when it is quiet: that is this job again,
/// redelivered after its worker died. While it still beats, another delivery
/// holds it (HeldElsewhere) and this one waits.
Claim try_start(long run_id, long stale_seconds, long quiet_seconds) {
    try {
        return Database::get().execute_write([&](auto& txn) -> Claim {
            txn.exec_params(std::string("UPDATE sync_runs SET status = 'interrupted', finished_at = now() "
                                        "WHERE status = 'running' AND id <> $3 AND ") +
                                SyncRunRepository::kQuietSql +
                                " AND xiaomi_user_id = (SELECT xiaomi_user_id FROM sync_runs WHERE id = $3)",
                            stale_seconds,
                            quiet_seconds,
                            run_id);
            // From queued, or from its own quiet running row. A closed run is
            // never revived: a redelivered job must not overwrite its journal
            // (Important 4).
            auto r = txn.exec_params(
                std::string("UPDATE sync_runs SET status = 'running', started_at = now(), heartbeat_at = now() "
                            "WHERE id = $3 AND (status = 'queued' OR (status = 'running' AND ") +
                    SyncRunRepository::kQuietSql + ")) RETURNING id",
                stale_seconds,
                quiet_seconds,
                run_id);
            if (!r.empty()) {
                return {SyncService::Start::Started, 0};
            }
            auto held = txn.exec_params(
                "SELECT GREATEST(0, CEIL(EXTRACT(EPOCH FROM (CASE WHEN heartbeat_at IS NULL "
                " THEN started_at + make_interval(secs => $1::double precision) "
                " ELSE heartbeat_at + make_interval(secs => $2::double precision) END - now())) * 1000))::bigint "
                "FROM sync_runs WHERE id = $3 AND status = 'running'",
                stale_seconds,
                quiet_seconds,
                run_id);
            if (!held.empty()) {
                // A second of margin: at the deadline itself the row is not quiet yet.
                return {SyncService::Start::HeldElsewhere, held[0][0].template as<long long>() + 1000};
            }
            return {SyncService::Start::Skipped, 0};
        });
    } catch (const pqxx::unique_violation&) {
        // one_running_sync_run_per_account: another run of the account is live.
        return {SyncService::Start::Skipped, 0};
    } catch (const std::exception& e) {
        // Anything else is the database failing, not a run in progress: the
        // queue retries the job, the journal still says queued.
        spdlog::warn("sync run {}: could not be claimed: {}", run_id, e.what());
        throw;
    }
}

/// The transport of one run: before each request to the cloud it refreshes
/// the run's heartbeat (at most every kHeartbeatEverySeconds), so a live run
/// never looks quiet to a redelivery of its job.
class HeartbeatTransport : public Xiaomi::HttpTransport {
public:
    HeartbeatTransport(Xiaomi::HttpTransport& inner, long run_id)
        : inner_(inner), run_id_(run_id), last_beat_(std::chrono::steady_clock::now()) {}

    Xiaomi::HttpResponse send(const Xiaomi::HttpRequest& request) override {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_beat_ >= std::chrono::seconds(SyncService::kHeartbeatEverySeconds)) {
            last_beat_ = now;
            try {
                Database::get().execute_write([&](auto& txn) {
                    txn.exec_params("UPDATE sync_runs SET heartbeat_at = now() WHERE id = $1 AND status = 'running'",
                                    run_id_);
                    return true;
                });
            } catch (const std::exception& e) {
                // A missed beat only shortens the time a redelivery waits.
                spdlog::warn("sync run {}: heartbeat not recorded: {}", run_id_, e.what());
            }
        }
        return inner_.send(request);
    }

private:
    Xiaomi::HttpTransport& inner_;
    long run_id_;
    std::chrono::steady_clock::time_point last_beat_;
};

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

void bump_sync_state(const std::string& xiaomi_user_id, const std::string& data_type, long added) {
    Database::get().execute_write([&](auto& txn) {
        txn.exec_params(
            "INSERT INTO sync_state (xiaomi_user_id, data_type, last_sync_at, records_count) "
            "VALUES ($3, $1, now(), $2) "
            "ON CONFLICT (xiaomi_user_id, data_type) DO UPDATE SET "
            "last_sync_at = now(), records_count = sync_state.records_count + $2",
            data_type,
            added,
            xiaomi_user_id);
        return true;
    });
}

}  // namespace

SyncService::SyncService(Xiaomi::HttpTransport& transport,
                         Xiaomi::Credentials credentials,
                         std::function<void(const Xiaomi::Credentials&)> on_rotate)
    : transport_(transport), credentials_(std::move(credentials)), on_rotate_(std::move(on_rotate)) {}

long SyncService::quiet_after_seconds() {
    return 3 * Xiaomi::Service::http_timeout_seconds() + 120;
}

long SyncService::stale_after_seconds() {
    long type_timeout = 180;
    if (Config::is_initialized()) {
        type_timeout =
            Config::get().get<long>("fitness.xiaomi.sync_type_timeout_seconds", "MI_FITNESS_SYNC_TYPE_TIMEOUT", 180);
    }
    return type_timeout * static_cast<long>(kAllDataTypes.size()) + 600;
}

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

    const long stale_seconds = stale_after_seconds();
    login_outcome_ = Login::NotAttempted;
    retry_in_ms_ = 0;
    const Claim claim = try_start(run_id, stale_seconds, quiet_after_seconds());
    start_outcome_ = claim.outcome;
    if (claim.outcome == Start::HeldElsewhere) {
        // The same run is alive in another delivery: neither the cloud nor the journal is touched.
        retry_in_ms_ = claim.retry_in_ms;
        return result;
    }
    if (claim.outcome == Start::Skipped) {
        // Another run holds running: honest skipped, the cloud is not touched.
        // Only a queued run gets its journal updated: a redelivered job must
        // not overwrite a finished run.
        result["skipped_reason"] = "another sync run is in progress";
        runs.skip_if_queued(run_id, result);
        return result;
    }

    HeartbeatTransport transport(transport_, run_id);
    Xiaomi::CloudClient client(transport, credentials_, on_rotate_);
    bool logged_in = false;
    // The error class of a failed login. One sign-in per run: whatever the
    // failure, the remaining types are closed without touching the network,
    // otherwise a Xiaomi that is down gets eight sign-ins per run.
    std::string login_error;
    bool any_failed = false;

    for (const auto& data_type : data_types) {
        auto& entry = result[data_type];
        const auto type_started = std::chrono::steady_clock::now();
        // A failed login is not retried within the run: an auth failure is
        // fixed by a fresh token (Important 5 of the final review), a network
        // or protocol failure by time. The remaining types are closed with the
        // same error class without touching the network.
        if (!login_error.empty()) {
            any_failed = true;
            entry["error"] = login_error;
            continue;
        }
        if (!logged_in) {
            try {
                client.login();
                logged_in = true;
                login_outcome_ = Login::Accepted;
            } catch (const Xiaomi::MiFitnessAuthError& e) {
                login_error = "auth";
                login_outcome_ = Login::Refused;
                spdlog::warn("sync {}: login refused: {}", data_type, e.what());
            } catch (const Xiaomi::MiFitnessProtocolError& e) {
                login_error = "protocol";
                spdlog::warn("sync {}: login failed: {}", data_type, e.what());
            } catch (const std::exception& e) {
                login_error = "other";
                spdlog::warn("sync {}: login failed: {}", data_type, e.what());
            }
            if (!logged_in) {
                any_failed = true;
                entry["error"] = login_error;
                continue;
            }
        }
        try {
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

            auto chunks = split_range(from, to, chunk_days);
            if (data_type == "daily_activity" && !chunks.empty()) {
                // The window is cut in the region's zone, the device may live
                // ahead of it or behind: local minutes of `from` and `to` fall
                // before and after the window. A day of margin on both sides
                // makes the edge days whole; days outside [from, to] are
                // dropped after normalizing, as before.
                chunks.front().from = shift_date(chunks.front().from, -1);
                chunks.back().to = shift_date(chunks.back().to, 1);
            }
            for (const auto& chunk : chunks) {
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
            bump_sync_state(credentials_.user_id, data_type, counts.added);
        } catch (const Xiaomi::MiFitnessAuthError& e) {
            any_failed = true;
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
