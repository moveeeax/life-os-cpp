/**
 * @file ContactRepository.hpp
 * @brief travel_trip_contacts: the people to reach on a trip (a host, a guide,
 *        a driver, an embassy), each with a role and a way to reach them.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"

namespace Repositories::Travel {

class ContactRepository {
public:
    struct Input {
        std::string name;
        std::string role;
        std::string phone;
        std::string email;
        std::string link;
        std::string note;
    };

    /// The trip's contacts by name.
    nlohmann::json list(const std::string& owner, const std::string& trip) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM travel_trip_contacts WHERE owner_id = $1::uuid AND trip_id = $2::uuid "
                                         " ORDER BY lower(name), id) t",
                                     owner,
                                     trip);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws NotFound when the trip is not the owner's.
    nlohmann::json add(const std::string& owner, const std::string& trip, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO travel_trip_contacts (owner_id, trip_id, name, role, phone, email, link, note) "
                        "SELECT $1::uuid, id, $3, $4, $5, $6, $7, $8 FROM travel_trips "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id::text",
                        owner,
                        trip,
                        in.name,
                        in.role,
                        in.phone,
                        in.email,
                        in.link,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("trip");
                    }
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// A full replacement. @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& trip, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE travel_trip_contacts SET name = $4, role = $5, phone = $6, email = $7, link = $8, "
                        " note = $9 WHERE owner_id = $1::uuid AND trip_id = $2::uuid AND id = $3::uuid RETURNING id",
                        owner,
                        trip,
                        id,
                        in.name,
                        in.role,
                        in.phone,
                        in.email,
                        in.link,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("trip_contact");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    void remove(const std::string& owner, const std::string& trip, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM travel_trip_contacts WHERE owner_id = $1::uuid AND trip_id = $2::uuid AND id = $3::uuid "
                "RETURNING id",
                owner,
                trip,
                id);
            if (r.empty()) {
                throw NotFound("trip_contact");
            }
            return true;
        });
    }

private:
    static constexpr const char* kColumns = "id, trip_id, name, role, phone, email, link, note";

    template <typename Txn>
    std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM travel_trip_contacts WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Travel
