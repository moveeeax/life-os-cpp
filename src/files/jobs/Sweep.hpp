/**
 * @file Sweep.hpp
 * @brief Worker job `files_sweep`: the clean-up of the private file store
 *        (migration 051). First it deletes the private_files rows still
 *        pending an hour after their create (a tab closed mid-upload); the
 *        trigger hands their keys to private_file_purges. Then it removes the
 *        object of every purge older than an hour and forgets the purge. A
 *        purge is a row deleted by the API, by a parent delete, by an account
 *        delete or by the step above. Payload `{}`.
 *
 *        An object the store would not remove keeps its purge for the next
 *        tick. Idempotent: a missing object is fine for the store. Same shape
 *        as now_media_sweep (now/jobs/MediaSweep.hpp).
 */

#pragma once

#include <exception>
#include <string>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "repositories/files/FileRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/Modules.hpp"

namespace Jobs::FilesSweep {

inline constexpr const char* kJobType = "files_sweep";

/// The presigned PUT lives 900 s (ObjectStore::private_upload_url): an hour leaves a slow upload
/// alone, and an object removed after that cannot be written again through an old URL.
inline constexpr int kStaleSeconds = 3600;
/// Rows and purges per run; the next tick takes the rest.
inline constexpr int kBatch = 200;

inline nlohmann::json process_job(const nlohmann::json& /*payload*/) {
    if (!Core::medical_enabled() && !Core::travel_enabled()) {
        return {{"status", "skipped"}, {"reason", "modules_disabled"}};
    }
    const Storage::ObjectStore* store = Storage::object_store();
    if (store == nullptr) {
        return {{"status", "skipped"}, {"reason", "storage_not_configured"}};
    }
    Repositories::Files::FileRepository repo;
    const int stale = repo.remove_stale_pending(kStaleSeconds, kBatch);
    int removed = 0;
    int failed = 0;
    for (const auto& key : repo.due_purges(kStaleSeconds, kBatch)) {
        try {
            store->remove(key);
        } catch (const std::exception& e) {
            spdlog::warn("files_sweep: the store kept {}: {}", key, e.what());
            ++failed;
            continue;
        }
        repo.forget_purge(key);
        ++removed;
    }
    if (stale + removed > 0) {
        spdlog::info("files_sweep: {} stale upload(s) dropped, {} object(s) removed", stale, removed);
    }
    return {{"status", "done"}, {"stale", stale}, {"removed", removed}, {"failed", failed}};
}

}  // namespace Jobs::FilesSweep
