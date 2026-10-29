/**
 * @file FactRepository.hpp
 * @brief ani_facts (migration 071): what Ani keeps about the person, at most
 *        kMax, each up to 200 characters; listed in her preface and in Account.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"

namespace Repositories::Agent {

class FactRepository {
public:
    static constexpr int kMax = 50;

    /// [{id, text, source, created_at}] oldest first.
    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return nlohmann::json::parse(
                txn.exec_params(
                       "SELECT COALESCE(json_agg(json_build_object('id', id::text, 'text', text, 'source', source, "
                       "'created_at', created_at) ORDER BY created_at), '[]'::json)::text FROM ani_facts WHERE "
                       "owner_id = $1::uuid",
                       owner)[0][0]
                    .template as<std::string>());
        });
    }

    /// The new fact's id; nullopt at kMax facts.
    std::optional<std::string> add(const std::string& owner, const std::string& text, const std::string& source) {
        return Database::get().execute_write([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params(
                "INSERT INTO ani_facts (owner_id, text, source) SELECT $1::uuid, $2, $3 WHERE (SELECT count(*) FROM "
                "ani_facts WHERE owner_id = $1::uuid) < $4 RETURNING id::text",
                owner,
                text,
                source,
                kMax);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    bool update(const std::string& owner, const std::string& id, const std::string& text) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params(
                          "UPDATE ani_facts SET text = $3 WHERE owner_id = $1::uuid AND id::text = $2", owner, id, text)
                       .affected_rows() > 0;
        });
    }

    bool remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            return txn.exec_params("DELETE FROM ani_facts WHERE owner_id = $1::uuid AND id::text = $2", owner, id)
                       .affected_rows() > 0;
        });
    }
};

}  // namespace Repositories::Agent
