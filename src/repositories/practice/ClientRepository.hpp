/**
 * @file ClientRepository.hpp
 * @brief practice_clients: the people and companies the practice works for.
 *        A client answers with its open deals and its income per currency
 *        (never summed across currencies).
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/practice/Errors.hpp"

namespace Repositories::Practice {

class ClientRepository {
public:
    struct Input {
        std::string name;
        std::string contact;
        std::string note;
    };

    struct Patch {
        std::optional<std::string> name;
        std::optional<std::string> contact;
        std::optional<std::string> note;
    };

    /// @throws Invariant.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO practice_clients (owner_id, name, contact, note) "
                        "VALUES ($1::uuid, $2, $3, $4) RETURNING id::text",
                        owner,
                        in.name,
                        in.contact,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    bool owns(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) {
            return !txn.exec_params(
                           "SELECT 1 FROM practice_clients WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id)
                        .empty();
        });
    }

    /// @throws NotFound, Invariant.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE practice_clients SET name = COALESCE($3, name), contact = COALESCE($4, contact), "
                        " note = COALESCE($5, note) WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.name,
                        p.contact,
                        p.note);
                    if (r.empty()) {
                        throw NotFound("client");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Its entries stay, unlinked. @throws NotFound, ClientHasDeals.
    void remove(const std::string& owner, const std::string& id) {
        detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    if (txn.exec_params(
                               "DELETE FROM practice_clients WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                               owner,
                               id)
                            .empty()) {
                        throw NotFound("client");
                    }
                    return true;
                });
            },
            [](std::string_view sqlstate) {
                if (Repositories::detail::is_fk_violation(sqlstate)) {
                    throw ClientHasDeals();
                }
            });
    }

    /// Every client of the owner by name.
    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM practice_clients c WHERE c.owner_id = $1::uuid "
                                         " ORDER BY lower(c.name), c.created_at) t",
                                     owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

private:
    static constexpr const char* kColumns =
        "c.id, c.name, c.contact, c.note, "
        " (SELECT count(*) FROM practice_deals d WHERE d.client_id = c.id "
        "   AND d.stage IN ('lead', 'negotiation', 'active'))::int AS open_deals, "
        " (SELECT COALESCE(json_agg(x ORDER BY x.currency), '[]'::json) FROM (SELECT e.currency, "
        "   SUM(e.amount) AS amount FROM practice_entries e WHERE e.client_id = c.id AND e.kind = 'income' "
        "   GROUP BY e.currency) x) AS income, "
        " " SQL_ISO_UTC("c.created_at") " AS created_at, "
        " " SQL_ISO_UTC("c.updated_at") " AS updated_at";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM practice_clients c WHERE c.owner_id = $1::uuid AND c.id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Practice
