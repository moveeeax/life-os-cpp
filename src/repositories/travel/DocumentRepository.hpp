/**
 * @file DocumentRepository.hpp
 * @brief The owner's travel documents (passports, visas, insurance) with their
 *        expiry, and the warnings they raise for a trip.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"
#include "travel/Documents.hpp"

namespace Repositories::Travel {

class DocumentRepository {
public:
    struct Input {
        std::string holder;
        std::string kind;  // passport | visa | insurance | other
        std::string number;
        std::string valid_until;
        std::string note;
    };

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO travel_documents (owner_id, holder, kind, number, valid_until, note) "
                        "VALUES ($1::uuid, $2, $3, $4, $5::date, $6) RETURNING id::text",
                        owner,
                        in.holder,
                        in.kind,
                        in.number,
                        in.valid_until,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// A full replacement. @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE travel_documents SET holder = $3, kind = $4, number = $5, valid_until = $6::date, "
                        " note = $7 WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.holder,
                        in.kind,
                        in.number,
                        in.valid_until,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("document");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM travel_documents WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("document");
            }
            return true;
        });
    }

    /// The soonest to expire first.
    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM travel_documents WHERE owner_id = $1::uuid "
                                         " ORDER BY valid_until, holder, created_at) t",
                                     owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// [{document_id, holder, kind, valid_until, reason}] for a trip ending on @p end.
    nlohmann::json warnings(const std::string& owner, const std::string& end) {
        const auto rows = list(owner);
        std::vector<::Travel::Document> docs;
        for (const auto& row : rows) {
            docs.push_back(
                {row["id"].get<std::string>(), row["kind"].get<std::string>(), row["valid_until"].get<std::string>()});
        }
        nlohmann::json out = nlohmann::json::array();
        for (const auto& w : ::Travel::warnings(docs, end)) {
            for (const auto& row : rows) {
                if (row["id"] == w.document_id) {
                    out.push_back({{"document_id", w.document_id},
                                   {"holder", row["holder"]},
                                   {"kind", row["kind"]},
                                   {"valid_until", row["valid_until"]},
                                   {"reason", w.reason}});
                }
            }
        }
        return out;
    }

private:
    static constexpr const char* kColumns =
        "id, holder, kind, number, to_char(valid_until, 'YYYY-MM-DD') AS valid_until, note";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM travel_documents WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Travel
