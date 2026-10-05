/**
 * @file FitnessSyncHandler.hpp
 * @brief fitness_sync job handler: a full SyncService run in the worker.
 *
 * Payload: {run_id, from, to, data_types}. Credentials are read from the
 * database with the key from config; their absence is not a queue failure but
 * an honest not_configured status in the run journal: retrying without a token is pointless.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "fitness/sync/SyncService.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/fitness/CredentialsRepository.hpp"
#include "repositories/fitness/SyncRunRepository.hpp"
#include "repositories/workout/SessionRepository.hpp"
#include "utils/Config.hpp"

namespace Jobs::FitnessSync {

inline constexpr const char* kJobType = "fitness_sync";

/**
 * @brief Enqueue a sync of the last @p window_days days.
 *
 * Days are counted in the region's zone (cn is UTC+8, others UTC, the same
 * rule as the range bounds in Xiaomi::range_to_timestamps): the UTC "today"
 * near the region's midnight lags a day behind, and the window would miss
 * the freshest data. Returns the run_id of the created run; database and
 * queue exceptions propagate to the caller.
 */
inline long enqueue_recent(int window_days, long long now_epoch) {
    if (window_days < 1) {
        window_days = 1;
    }
    std::string region;
    if (Config::is_initialized()) {
        region = Config::get().get<std::string>("fitness.xiaomi.region", "MI_FITNESS_REGION", "");
    }
    const long long offset = (region.empty() || region == "cn") ? 8 * 3600 : 0;

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

    Repositories::SyncRunRepository runs;
    const long run_id = runs.create(from, to, Sync::kAllDataTypes);
    Jobs::get().submit(
        kJobType, nlohmann::json{{"run_id", run_id}, {"from", from}, {"to", to}, {"data_types", Sync::kAllDataTypes}});
    return run_id;
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
    Repositories::CredentialsRepository credentials_repo(token_key);
    const auto credentials = credentials_repo.load();
    if (!credentials.has_value()) {
        runs.finish(run_id,
                    "failed",
                    nlohmann::json{{"error", "not_configured"}, {"detail", "Xiaomi credentials are not seeded"}});
        return {{"run_id", run_id}, {"status", "failed"}};
    }

    Sync::SyncService service(
        Xiaomi::Service::transport(), *credentials, [&credentials_repo](const Xiaomi::Credentials& rotated) {
            credentials_repo.store(rotated);
        });
    const auto result = service.run(run_id, from, to, data_types);

    // Fresh band data may belong to a logged workout session. A failure here
    // must not fail the sync that has already been stored.
    if (Core::workout_enabled()) {
        try {
            const int window =
                Config::get().get<int>("fitness.xiaomi.sync_window_days", "MI_FITNESS_SYNC_WINDOW_DAYS", 2);
            const long n = Repositories::SessionRepository().reconcile_recent(window + 1);
            spdlog::info("workout reconcile after fitness sync {}: {} sessions", run_id, n);
        } catch (const std::exception& e) {
            spdlog::warn("workout reconcile after fitness sync {} failed: {}", run_id, e.what());
        }
    }
    return {{"run_id", run_id}, {"result", result}};
}

}  // namespace Jobs::FitnessSync
