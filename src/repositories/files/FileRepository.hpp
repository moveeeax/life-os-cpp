/**
 * @file FileRepository.hpp
 * @brief private_files: the owner's attachments of a medical record or a
 *        travel document, and private_file_purges, the object keys whose row
 *        is gone (migration 051). Every query names the owner; a row of
 *        another owner reads as absent. A DELETE of a row leaves its key in
 *        the purges through a trigger, in the same transaction, so an object
 *        is never forgotten: the files_sweep job removes it.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories::Files {

struct FileNotFound : NotFoundError {
    FileNotFound() : NotFoundError("file") {}
};

/// The record or document a file is attached to is not the caller's (or not there).
struct ParentNotFound : NotFoundError {
    ParentNotFound() : NotFoundError("parent") {}
};

struct TooManyFiles : ConflictError {
    explicit TooManyFiles(int max)
        : ConflictError("too_many_files", "a record or document holds at most " + std::to_string(max) + " files") {}
};

struct FileInvariant : ValidationError {
    FileInvariant() : ValidationError("invariant", "the row breaks a rule of the file store") {}
};

/// The parents a file can hang on, and the table each lives in.
inline const char* parent_table(std::string_view kind) {
    if (kind == "medical_record") {
        return "medical_records";
    }
    if (kind == "travel_document") {
        return "travel_documents";
    }
    return nullptr;
}

class FileRepository {
public:
    /// Files per parent, pending ones included.
    static constexpr int kMaxPerParent = 20;

    struct Input {
        std::string id;
        std::string parent_kind;
        std::string parent_id;
        std::string name;
        std::string content_type;
        long long bytes = 0;
        std::string object_key;
    };

    /// A fresh id, so the object key can be written with the row.
    std::string new_id() {
        return Database::get().execute_read(
            [](auto& txn) { return txn.exec("SELECT gen_random_uuid()::text")[0][0].template as<std::string>(); });
    }

    /// Whether the parent row exists and is the owner's.
    bool parent_exists(const std::string& owner, const std::string& kind, const std::string& parent_id) {
        const char* table = parent_table(kind);
        if (table == nullptr) {
            return false;
        }
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           std::string("SELECT 1 FROM ") + table + " WHERE owner_id = $1::uuid AND id = $2::uuid",
                           owner,
                           parent_id)
                        .empty();
        });
    }

    /// A pending row. @throws ParentNotFound, TooManyFiles.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    const char* table = parent_table(in.parent_kind);
                    // The parent row is locked so two uploads at once cannot both pass the count.
                    if (table == nullptr ||
                        txn.exec_params(std::string("SELECT 1 FROM ") + table +
                                            " WHERE owner_id = $1::uuid AND id = $2::uuid FOR UPDATE",
                                        owner,
                                        in.parent_id)
                            .empty()) {
                        throw ParentNotFound();
                    }
                    auto n = txn.exec_params(
                        "SELECT count(*) FROM private_files WHERE owner_id = $1::uuid AND parent_kind = $2 "
                        " AND parent_id = $3::uuid",
                        owner,
                        in.parent_kind,
                        in.parent_id);
                    if (n[0][0].template as<long long>() >= kMaxPerParent) {
                        throw TooManyFiles(kMaxPerParent);
                    }
                    txn.exec_params(
                        "INSERT INTO private_files (id, owner_id, parent_kind, parent_id, name, content_type, bytes, "
                        " object_key) VALUES ($1::uuid, $2::uuid, $3, $4::uuid, $5, $6, $7, $8)",
                        in.id,
                        owner,
                        in.parent_kind,
                        in.parent_id,
                        in.name,
                        in.content_type,
                        in.bytes,
                        in.object_key);
                    return *find_in(txn, owner, in.id);
                });
            },
            &translate);
    }

    /// The owner's row, pending or ready, with its object key; nullopt for anyone else's.
    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// The ready files of one parent, oldest first.
    nlohmann::json list(const std::string& owner, const std::string& kind, const std::string& parent_id) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM private_files WHERE owner_id = $1::uuid AND parent_kind = $2 "
                                         " AND parent_id = $3::uuid AND status = 'ready' ORDER BY created_at, id) t",
                                     owner,
                                     kind,
                                     parent_id);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The number of ready files of each of the owner's parents of @p kind: {parent_id: n}.
    nlohmann::json counts(const std::string& owner, const std::string& kind) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_object_agg(parent_id::text, n), '{}'::json) FROM (SELECT parent_id, count(*) AS "
                "n "
                " FROM private_files WHERE owner_id = $1::uuid AND parent_kind = $2 AND status = 'ready' "
                " GROUP BY parent_id) t",
                owner,
                kind);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws FileNotFound.
    nlohmann::json mark_ready(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE private_files SET status = 'ready', ready_at = COALESCE(ready_at, now()) "
                "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                owner,
                id);
            if (r.empty()) {
                throw FileNotFound();
            }
            return *find_in(txn, owner, id);
        });
    }

    /// Deletes the row (its key goes to the purges in the same transaction) and
    /// answers the key, so the caller can remove the object at once. @throws FileNotFound.
    std::string remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM private_files WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING object_key",
                owner,
                id);
            if (r.empty()) {
                throw FileNotFound();
            }
            return r[0][0].template as<std::string>();
        });
    }

    /// Deletes up to @p limit rows still pending @p older_than_seconds after their create; their
    /// keys go to the purges. Answers how many went.
    int remove_stale_pending(int older_than_seconds, int limit) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM private_files WHERE id IN (SELECT id FROM private_files WHERE status = 'pending' "
                " AND created_at < now() - make_interval(secs => $1::float8) ORDER BY created_at, id LIMIT $2) "
                "RETURNING id",
                older_than_seconds,
                limit);
            return static_cast<int>(r.size());
        });
    }

    /// Purged keys older than @p older_than_seconds, oldest first: by then no presigned
    /// PUT of theirs is still valid, so a removed object cannot come back.
    std::vector<std::string> due_purges(int older_than_seconds, int limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT object_key FROM private_file_purges "
                " WHERE created_at < now() - make_interval(secs => $1::float8) ORDER BY created_at, object_key LIMIT "
                "$2",
                older_than_seconds,
                limit);
            std::vector<std::string> out;
            out.reserve(r.size());
            for (const auto& row : r) {
                out.push_back(row[0].template as<std::string>());
            }
            return out;
        });
    }

    /// Forgets a purge once its object is gone.
    void forget_purge(const std::string& key) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params("DELETE FROM private_file_purges WHERE object_key = $1", key);
            return true;
        });
    }

private:
    static constexpr const char* kColumns =
        "id::text AS id, parent_kind, parent_id::text AS parent_id, name, content_type, bytes, object_key, status, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at";

    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23514" || sqlstate == "23505") {  // check_violation, unique_violation
            throw FileInvariant();
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM private_files WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Files
