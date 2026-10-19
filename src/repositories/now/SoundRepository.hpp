/**
 * @file SoundRepository.hpp
 * @brief sounds: the shared library of focus sounds. Rows are global (no
 *        owner), written by admins only; the objects live in S3 under the keys
 *        stored here.
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

struct SoundNotFound : NotFoundError {
    SoundNotFound() : NotFoundError("sound") {}
};

struct SoundInvariant : ValidationError {
    SoundInvariant() : ValidationError("invariant", "the row breaks a rule of the sound library") {}
};

class SoundRepository {
public:
    struct Input {
        std::string id;
        std::string title;
        std::string object_key;
        std::string content_type;
        long long bytes = 0;
        std::optional<int> duration_seconds;
        std::string created_by;
    };

    /// A fresh id, so the object key can be written with the row.
    std::string new_id() {
        return Database::get().execute_read(
            [](auto& txn) { return txn.exec("SELECT gen_random_uuid()::text")[0][0].template as<std::string>(); });
    }

    nlohmann::json create(const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    txn.exec_params(
                        "INSERT INTO sounds (id, title, object_key, content_type, bytes, duration_seconds, created_by) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7::uuid)",
                        in.id,
                        in.title,
                        in.object_key,
                        in.content_type,
                        in.bytes,
                        in.duration_seconds,
                        in.created_by);
                    return *find_in(txn, in.id);
                });
            },
            &translate);
    }

    std::optional<nlohmann::json> get(const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, id); });
    }

    /// @throws SoundNotFound.
    nlohmann::json mark_ready(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("UPDATE sounds SET status = 'ready' WHERE id = $1::uuid RETURNING id", id);
            if (r.empty()) {
                throw SoundNotFound();
            }
            return *find_in(txn, id);
        });
    }

    /// Takes the row out of the players before its object goes: a delete that fails half way
    /// leaves a pending row for the sweep, never a ready row whose object is gone.
    /// @throws SoundNotFound.
    void mark_pending(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("UPDATE sounds SET status = 'pending' WHERE id = $1::uuid RETURNING id", id);
            if (r.empty()) {
                throw SoundNotFound();
            }
            return true;
        });
    }

    /// Pending rows created more than @p older_than_seconds ago, oldest first, at most @p limit.
    nlohmann::json stale_pending(int older_than_seconds, int limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM sounds WHERE status = 'pending' "
                                         " AND created_at < now() - make_interval(secs => $1::float8) "
                                         " ORDER BY created_at, id LIMIT $2) t",
                                     older_than_seconds,
                                     limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Deletes the row while it is still pending; false when it is gone or became ready.
    bool remove_pending(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return !txn.exec_params("DELETE FROM sounds WHERE id = $1::uuid AND status = 'pending' RETURNING id", id)
                        .empty();
        });
    }

    /// Every row, pending ones included, newest first.
    nlohmann::json list_all() {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                              " FROM sounds ORDER BY created_at DESC, id) t");
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The ready sounds by title.
    nlohmann::json list_ready() {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                              " FROM sounds WHERE status = 'ready' ORDER BY title, id) t");
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws SoundNotFound.
    void remove(const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("DELETE FROM sounds WHERE id = $1::uuid RETURNING id", id);
            if (r.empty()) {
                throw SoundNotFound();
            }
            return true;
        });
    }

private:
    static constexpr const char* kColumns =
        "id::text AS id, title, object_key, content_type, bytes, duration_seconds, status, "
        "created_by::text AS created_by, created_at";

    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23514") {  // check_violation
            throw SoundInvariant();
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) + " FROM sounds WHERE id = $1::uuid) t", id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Now
