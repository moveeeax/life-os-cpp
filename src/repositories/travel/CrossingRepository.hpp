/**
 * @file CrossingRepository.hpp
 * @brief travel_crossings: the owner's border crossings, the record every day
 *        counter is built from.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"
#include "travel/Days.hpp"

namespace Repositories::Travel {

class CrossingRepository {
public:
    struct Input {
        std::string date;
        std::string from;
        std::string to;
        std::optional<std::string> time;  // HH:MM
        std::string place;
        std::string note;
    };

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO travel_crossings (owner_id, date, from_country, to_country, time, place, note) "
                        "VALUES ($1::uuid, $2::date, $3, $4, $5::time, $6, $7) RETURNING id::text",
                        owner,
                        in.date,
                        in.from,
                        in.to,
                        in.time,
                        in.place,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// A full replacement of the crossing's fields. @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE travel_crossings SET date = $3::date, from_country = $4, to_country = $5, "
                        " time = $6::time, place = $7, note = $8 "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.date,
                        in.from,
                        in.to,
                        in.time,
                        in.place,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("crossing");
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
                "DELETE FROM travel_crossings WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("crossing");
            }
            return true;
        });
    }

    /// Newest first, a page.
    nlohmann::json list(const std::string& owner, int limit, int offset) {
        return Database::get().execute_read([&](auto& txn) {
            auto r =
                txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                    " FROM travel_crossings WHERE owner_id = $1::uuid "
                                    " ORDER BY date DESC, time DESC NULLS LAST, created_at DESC LIMIT $2 OFFSET $3) t",
                                owner,
                                limit,
                                offset);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    long long total(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return txn.exec_params("SELECT count(*) FROM travel_crossings WHERE owner_id = $1::uuid", owner)[0][0]
                .template as<long long>();
        });
    }

    /// Every crossing in date order, for the day counters.
    std::vector<::Travel::Crossing> all(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT to_char(date, 'YYYY-MM-DD'), from_country, to_country FROM travel_crossings "
                "WHERE owner_id = $1::uuid ORDER BY date, time NULLS FIRST, created_at",
                owner);
            std::vector<::Travel::Crossing> out;
            out.reserve(r.size());
            for (const auto& row : r) {
                out.push_back({row[0].template as<std::string>(),
                               row[1].template as<std::string>(),
                               row[2].template as<std::string>()});
            }
            return out;
        });
    }

private:
    static constexpr const char* kColumns =
        "id, to_char(date, 'YYYY-MM-DD') AS date, from_country, to_country, to_char(time, 'HH24:MI') AS time, place, "
        "note";

    template <typename Txn>
    std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM travel_crossings WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Travel
