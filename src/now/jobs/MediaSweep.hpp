/**
 * @file MediaSweep.hpp
 * @brief Worker job `now_media_sweep`: deletes the wallpaper and sound rows
 *        left pending past the upload window, with their objects. A create
 *        whose browser never PUTs or calls /ready (a tab closed mid-upload)
 *        leaves such a row, and so does a delete whose store call failed
 *        (the delete marks the row pending first). Payload `{}`.
 *
 *        A row whose objects the store would not remove stays for the next
 *        tick; the row goes only after its objects, and only while it is
 *        still pending. Idempotent: a missing object is fine for the store.
 */

#pragma once

#include <exception>
#include <initializer_list>
#include <string>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "repositories/now/SoundRepository.hpp"
#include "repositories/now/WallpaperRepository.hpp"
#include "storage/ObjectStore.hpp"
#include "utils/Modules.hpp"

namespace Jobs::NowMediaSweep {

inline constexpr const char* kJobType = "now_media_sweep";

/// The presigned PUT lives 900 s (ObjectStore::upload_url); an hour leaves a slow upload and its /ready alone.
inline constexpr int kStaleSeconds = 3600;
/// Rows per table per run; the next tick takes the rest.
inline constexpr int kBatch = 200;

namespace detail {

struct Counts {
    int removed = 0;
    int failed = 0;
};

/// Removes each row's objects (the @p keys fields that are strings), then the row.
template <typename Repo>
Counts sweep(Repo& repo, const Storage::ObjectStore& store, std::initializer_list<const char*> keys, const char* what) {
    Counts out;
    for (const auto& row : repo.stale_pending(kStaleSeconds, kBatch)) {
        const std::string id = row.at("id").template get<std::string>();
        try {
            for (const char* k : keys) {
                if (row.contains(k) && row[k].is_string()) {
                    store.remove(row[k].template get<std::string>());
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("now_media_sweep: the store kept {} {}: {}", what, id, e.what());
            ++out.failed;
            continue;
        }
        if (repo.remove_pending(id)) {
            ++out.removed;
        }
    }
    return out;
}

}  // namespace detail

inline nlohmann::json process_job(const nlohmann::json& /*payload*/) {
    if (!Core::now_enabled()) {
        return {{"status", "skipped"}, {"reason", "now_disabled"}};
    }
    const Storage::ObjectStore* store = Storage::object_store();
    if (store == nullptr) {
        return {{"status", "skipped"}, {"reason", "storage_not_configured"}};
    }
    Repositories::Now::WallpaperRepository wallpapers;
    Repositories::Now::SoundRepository sounds;
    const auto w = detail::sweep(wallpapers, *store, {"object_key", "poster_key"}, "wallpaper");
    const auto s = detail::sweep(sounds, *store, {"object_key"}, "sound");
    if (w.removed + s.removed > 0) {
        spdlog::info("now_media_sweep: removed {} wallpaper(s) and {} sound(s) left pending", w.removed, s.removed);
    }
    return {{"status", "done"}, {"wallpapers", w.removed}, {"sounds", s.removed}, {"failed", w.failed + s.failed}};
}

}  // namespace Jobs::NowMediaSweep
