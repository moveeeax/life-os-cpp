/**
 * @file WallpaperRepository.hpp
 * @brief wallpapers: the shared library behind every screen. Rows are global
 *        (no owner), written by admins only; the objects live in S3 under the
 *        keys stored here.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Now {

struct WallpaperNotFound : NotFoundError {
    WallpaperNotFound() : NotFoundError("wallpaper") {}
};

struct WallpaperInvariant : ValidationError {
    WallpaperInvariant() : ValidationError("invariant", "the row breaks a rule of the wallpaper library") {}
};

class WallpaperRepository {
public:
    struct Input {
        std::string id;
        std::string kind;
        std::string device;
        std::string object_key;
        std::optional<std::string> poster_key;
        std::string content_type;
        long long bytes = 0;
        std::optional<long long> poster_bytes;
        std::optional<int> width;
        std::optional<int> height;
        std::string created_by;
    };

    /// A fresh id, so the object keys can be written with the row.
    std::string new_id() {
        return Database::get().execute_read(
            [](auto& txn) { return txn.exec("SELECT gen_random_uuid()::text")[0][0].template as<std::string>(); });
    }

    nlohmann::json create(const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    txn.exec_params(
                        "INSERT INTO wallpapers (id, kind, device, object_key, poster_key, content_type, bytes, "
                        " poster_bytes, width, height, created_by) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11::uuid)",
                        in.id,
                        in.kind,
                        in.device,
                        in.object_key,
                        in.poster_key,
                        in.content_type,
                        in.bytes,
                        in.poster_bytes,
                        in.width,
                        in.height,
                        in.created_by);
                    return *find_in(txn, in.id);
                });
            },
            &translate);
    }

    std::optional<nlohmann::json> get(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, id); });
    }

    /// @throws WallpaperNotFound.
    nlohmann::json mark_ready(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("UPDATE wallpapers SET status = 'ready' WHERE id = $1::uuid RETURNING id", id);
            if (r.empty()) {
                throw WallpaperNotFound();
            }
            return *find_in(txn, id);
        });
    }

    /// Every row, pending ones included, newest first.
    nlohmann::json list_all() {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                              " FROM wallpapers ORDER BY created_at DESC, id) t");
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The ready rows of one device, oldest first.
    nlohmann::json list_ready(const std::string& device) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM wallpapers WHERE status = 'ready' AND device = $1 "
                                         " ORDER BY created_at, id) t",
                                     device);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws WallpaperNotFound.
    void remove(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("DELETE FROM wallpapers WHERE id = $1::uuid RETURNING id", id);
            if (r.empty()) {
                throw WallpaperNotFound();
            }
            return true;
        });
    }

private:
    static constexpr const char* kColumns =
        "id::text AS id, kind, device, object_key, poster_key, content_type, bytes, poster_bytes, width, height, "
        "status, created_by::text AS created_by, created_at";

    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23514") {  // check_violation
            throw WallpaperInvariant();
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) + " FROM wallpapers WHERE id = $1::uuid) t",
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Now
