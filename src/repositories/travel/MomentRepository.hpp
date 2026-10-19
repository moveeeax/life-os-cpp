/**
 * @file MomentRepository.hpp
 * @brief travel_trip_moments: what happened on a trip, a dated line of text
 *        with an optional place.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"

namespace Repositories::Travel {

class MomentRepository {
public:
    struct Input {
        std::string date;
        std::string text;
        std::string place;
    };

    /// The trip's moments, the earliest first.
    nlohmann::json list(const std::string& owner, const std::string& trip) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM travel_trip_moments WHERE owner_id = $1::uuid AND trip_id = $2::uuid "
                                         " ORDER BY date, created_at, id) t",
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
                        "INSERT INTO travel_trip_moments (owner_id, trip_id, date, text, place) "
                        "SELECT $1::uuid, id, $3::date, $4, $5 FROM travel_trips "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id::text",
                        owner,
                        trip,
                        in.date,
                        in.text,
                        in.place);
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
                        "UPDATE travel_trip_moments SET date = $4::date, text = $5, place = $6 "
                        "WHERE owner_id = $1::uuid AND trip_id = $2::uuid AND id = $3::uuid RETURNING id",
                        owner,
                        trip,
                        id,
                        in.date,
                        in.text,
                        in.place);
                    if (r.empty()) {
                        throw NotFound("trip_moment");
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
                "DELETE FROM travel_trip_moments WHERE owner_id = $1::uuid AND trip_id = $2::uuid AND id = $3::uuid "
                "RETURNING id",
                owner,
                trip,
                id);
            if (r.empty()) {
                throw NotFound("trip_moment");
            }
            return true;
        });
    }

private:
    static constexpr const char* kColumns = "id, trip_id, to_char(date, 'YYYY-MM-DD') AS date, text, place";

    template <typename Txn>
    std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM travel_trip_moments WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Travel
