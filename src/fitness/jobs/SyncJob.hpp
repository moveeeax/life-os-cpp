/**
 * @file SyncJob.hpp
 * @brief fitness_sync job handler: a full SyncService run in the worker.
 *
 * Payload: {run_id, owner_id, from, to, data_types}. Credentials are those of the linked
 * Mi account, read from the database with the key from config; no linked
 * account is not a queue failure but an honest not_linked status in the run
 * journal: retrying without a token is pointless.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "fitness/sync/SyncService.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/Job.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "repositories/workout/SessionRepository.hpp"
#include "utils/Config.hpp"
#include "utils/Modules.hpp"

namespace Jobs::FitnessSync {

inline constexpr const char* kJobType = "fitness_sync";

/// Create the run of an account and put its job on the queue. Returns the run id.
inline long submit(const std::string& owner_id,
                   const std::string& xiaomi_user_id,
                   const std::string& from,
                   const std::string& to,
                   const std::vector<std::string>& data_types) {
    Repositories::SyncRunRepository runs;
    const long run_id = runs.create(xiaomi_user_id, from, to, data_types);
    Jobs::get().submit(
        kJobType,
        nlohmann::json{
            {"run_id", run_id}, {"owner_id", owner_id}, {"from", from}, {"to", to}, {"data_types", data_types}});
    return run_id;
}

/**
 * @brief Enqueue a sync of the last @p window_days days of one account.
 *
 * Days are counted in the zone of the account's region (cn is UTC+8, others
 * UTC, the same rule as the range bounds in Xiaomi::range_to_timestamps): the
 * UTC "today" near the region's midnight lags a day behind, and the window
 * would miss the freshest data. Returns the run_id of the created run;
 * database and queue exceptions propagate to the caller.
 */
inline long enqueue_recent(const Repositories::MiAccountRepository::Linked& account,
                           int window_days,
                           long long now_epoch) {
    if (window_days < 1) {
        window_days = 1;
    }
    const long long offset = (account.region.empty() || account.region == "cn") ? 8 * 3600 : 0;

    const auto day = [](long long epoch) {
        const std::chrono::sys_days d{
            std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds{std::chrono::seconds{epoch}})};
        const std::chrono::year_month_day ymd{d};
        char out[16];
        std::snprintf(out,
                      sizeof(out),
                      "%04d-%02u-%02u",
                      static_cast<int>(ymd.year()),
                      static_cast<unsigned>(ymd.month()),
                      static_cast<unsigned>(ymd.day()));
        return std::string(out);
    };
    const std::string to = day(now_epoch + offset);
    const std::string from = day(now_epoch + offset - 86400LL * (window_days - 1));
    return submit(account.owner_id, account.xiaomi_user_id, from, to, Sync::kAllDataTypes);
}

/**
 * @brief The scheduled tick: one job per account whose token Xiaomi still
 *        accepts. An account that asks to be linked again is skipped, its
 *        sync could only fail. An account with a run queued in the last
 *        @p skip_within_seconds (and not failed) is skipped too: the tick
 *        that runs after every API start would otherwise queue a whole
 *        round of syncs per deploy in front of the parse jobs. Returns how
 *        many jobs were enqueued; a failure on one account does not stop the
 *        others.
 */
inline int enqueue_recent_for_all(int window_days, long long now_epoch, long skip_within_seconds = 0) {
    int enqueued = 0;
    Repositories::SyncRunRepository runs;
    for (const auto& account : Repositories::MiAccountRepository("").list_syncable()) {
        try {
            if (skip_within_seconds > 0 && runs.recent_run(account.xiaomi_user_id, skip_within_seconds)) {
                continue;
            }
            enqueue_recent(account, window_days, now_epoch);
            ++enqueued;
        } catch (const std::exception& e) {
            spdlog::warn("fitness sync schedule: account {} was not enqueued: {}",
                         Xiaomi::mask_account_id(account.xiaomi_user_id),
                         e.what());
        }
    }
    return enqueued;
}

/**
 * @brief Dead-letter hook: the queue gave the job up. Its run is closed as
 *        failed if it is still queued, or running with a quiet heartbeat, so
 *        the page polling it stops waiting and the account's slot is free.
 */
inline void on_dead_letter(const nlohmann::json& payload, const std::string& error) {
    if (!payload.contains("run_id") || !payload["run_id"].is_number_integer()) {
        return;
    }
    const long run_id = payload["run_id"].get<long>();
    // The queue's error text is not stored: it may carry details of the failure.
    (void)error;
    const bool closed = Repositories::SyncRunRepository().fail_abandoned(
        run_id,
        nlohmann::json{{"error", "dead_lettered"}, {"detail", "the sync job failed and will not be retried"}},
        Sync::SyncService::stale_after_seconds(),
        Sync::SyncService::quiet_after_seconds());
    if (closed) {
        spdlog::warn("fitness sync {}: the job was dead-lettered, the run is closed as failed", run_id);
    }
}

inline nlohmann::json process_job(const nlohmann::json& payload) {
    const long run_id = payload.at("run_id").get<long>();
    const std::string from = payload.at("from").get<std::string>();
    const std::string to = payload.at("to").get<std::string>();
    std::vector<std::string> data_types = Sync::kAllDataTypes;
    if (payload.contains("data_types") && payload["data_types"].is_array() && !payload["data_types"].empty()) {
        data_types = payload["data_types"].get<std::vector<std::string>>();
    }

    if (!Core::fitness_enabled()) {
        // The job reached a worker with the module disabled: the journal gets
        // an honest status instead of endless retries into the DLQ.
        Repositories::SyncRunRepository runs_off;
        runs_off.finish(run_id, "failed", nlohmann::json{{"error", "fitness_disabled"}});
        return {{"run_id", run_id}, {"status", "failed"}};
    }

    Repositories::SyncRunRepository runs;
    const std::string token_key = Xiaomi::Service::token_key_b64();
    if (token_key.empty()) {
        runs.finish(run_id,
                    "failed",
                    nlohmann::json{{"error", "not_configured"}, {"detail", "MI_FITNESS_TOKEN_KEY is not set"}});
        return {{"run_id", run_id}, {"status", "failed"}};
    }
    // The run belongs to one account. A job enqueued before runs had an owner
    // cannot be attributed.
    if (!payload.contains("owner_id") || !payload["owner_id"].is_string()) {
        runs.finish(run_id, "failed", nlohmann::json{{"error", "outdated_job"}});
        return {{"run_id", run_id}, {"status", "failed"}};
    }
    const std::string owner_id = payload["owner_id"].get<std::string>();
    Repositories::MiAccountRepository accounts(token_key);
    std::optional<Repositories::MiAccountRepository::Fenced> loaded;
    try {
        loaded = accounts.load_fenced(owner_id);
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        // The stored token cannot be read: MI_FITNESS_TOKEN_KEY was rotated or
        // is wrong on this worker, or the row is corrupt. Retrying cannot help
        // and only a new link fixes it, the same as a probe concludes. Any
        // other error (the database) propagates and the queue retries.
        spdlog::warn("fitness sync {}: the stored token is unreadable: {}", run_id, e.what());
        runs.finish(
            run_id,
            "failed",
            nlohmann::json{{"error", "token_unreadable"}, {"detail", "the stored Xiaomi token cannot be read"}});
        try {
            accounts.mark_reauth_required(owner_id, "token_unreadable", accounts.rotated_at(owner_id).value_or(""));
        } catch (const std::exception& mark_error) {
            spdlog::warn("fitness sync {}: failed to record the link status: {}", run_id, mark_error.what());
        }
        return {{"run_id", run_id}, {"status", "failed"}};
    }
    if (!loaded.has_value()) {
        runs.finish(run_id, "failed", nlohmann::json{{"error", "not_linked"}, {"detail", "no Mi account is linked"}});
        return {{"run_id", run_id}, {"status", "failed"}};
    }
    // The link as it was loaded: a re-link during the run must not be undone by this run's writes.
    std::string fence = loaded->rotated_at;

    Sync::SyncService service(Xiaomi::Service::transport(),
                              loaded->credentials,
                              [&accounts, &owner_id, &fence](const Xiaomi::Credentials& rotated) {
                                  accounts.store_rotated(owner_id, rotated, fence);
                              });
    const auto result = service.run(run_id, from, to, data_types);
    if (service.start_outcome() == Sync::SyncService::Start::HeldElsewhere) {
        // The run's own row still beats under another delivery of this job (a
        // lease reaped while the first worker is alive). This one waits without
        // spending an attempt and takes the run over if that heartbeat stops.
        throw Jobs::DeferJob("fitness_sync: the run is held by another delivery", service.retry_in_ms());
    }

    // The link status follows what Xiaomi said to the stored token. A run that
    // never reached Xiaomi says nothing about it.
    try {
        if (service.login_outcome() == Sync::SyncService::Login::Accepted) {
            accounts.mark_ok(owner_id);
        } else if (service.login_outcome() == Sync::SyncService::Login::Refused) {
            accounts.mark_reauth_required(owner_id, "upstream_auth", fence);
        }
    } catch (const std::exception& e) {
        spdlog::warn("fitness sync {}: failed to record the link status: {}", run_id, e.what());
    }

    // Fresh band data may belong to a logged workout session. A failure here
    // must not fail the sync that has already been stored.
    if (Core::workout_enabled()) {
        try {
            const int window =
                Config::get().get<int>("fitness.xiaomi.sync_window_days", "MI_FITNESS_SYNC_WINDOW_DAYS", 2);
            const long n = Repositories::SessionRepository().reconcile_recent(owner_id, window + 1);
            spdlog::info("workout reconcile after fitness sync {}: {} sessions", run_id, n);
        } catch (const std::exception& e) {
            spdlog::warn("workout reconcile after fitness sync {} failed: {}", run_id, e.what());
        }
    }
    return {{"run_id", run_id}, {"result", result}};
}

}  // namespace Jobs::FitnessSync
