/**
 * @file TripRepository.hpp
 * @brief travel_trips and travel_trip_items: the owner's trips with their plan
 *        by day, plus the links other modules keep to a trip — packing tasks
 *        (task_items.trip_id) and spends (money_transactions.trip_id).
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"

namespace Repositories::Travel {

class TripRepository {
public:
    struct Input {
        std::string name;
        std::string start_date;
        std::string end_date;
        std::set<std::string> countries;
        std::string note;
    };

    struct Patch {
        std::optional<std::string> name;
        std::optional<std::string> start_date;
        std::optional<std::string> end_date;
        std::optional<std::set<std::string>> countries;
        std::optional<std::string> note;
    };

    struct ItemInput {
        std::string kind;  // flight | stay | booking | note
        std::string date;
        std::optional<std::string> time;  // HH:MM
        std::string title;
        std::string reference;
        std::string link;
        std::string note;
        bool no_crossing = false;  // a domestic flight, or one the owner is not on
    };

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO travel_trips (owner_id, name, start_date, end_date, countries, note) "
                        "VALUES ($1::uuid, $2, $3::date, $4::date, $5::text[], $6) RETURNING id::text",
                        owner,
                        in.name,
                        in.start_date,
                        in.end_date,
                        detail::pg_array(in.countries),
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// Every trip of the owner, the latest start first.
    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM travel_trips WHERE owner_id = $1::uuid ORDER BY start_date DESC, id) t",
                                     owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The trip starting first on or after @p date (the next one), if any.
    std::optional<nlohmann::json> next(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                         " FROM travel_trips WHERE owner_id = $1::uuid AND start_date >= $2::date "
                                         " ORDER BY start_date, id LIMIT 1) t",
                                     owner,
                                     date);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws NotFound.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    std::optional<std::string> countries;
                    if (p.countries) {
                        countries = detail::pg_array(*p.countries);
                    }
                    auto r = txn.exec_params(
                        "UPDATE travel_trips SET name = COALESCE($3, name), "
                        " start_date = COALESCE($4::date, start_date), end_date = COALESCE($5::date, end_date), "
                        " countries = COALESCE($6::text[], countries), note = COALESCE($7, note) "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.name,
                        p.start_date,
                        p.end_date,
                        countries,
                        p.note);
                    if (r.empty()) {
                        throw NotFound("trip");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// The trip and its plan go; its tasks and spends stay, unlinked (ON DELETE SET NULL).
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM travel_trips WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("trip");
            }
            return true;
        });
    }

    // ── the plan ────────────────────────────────────────────────────────────

    nlohmann::json items(const std::string& owner, const std::string& trip) {
        return Database::get().execute_read([&](auto& txn) {
            auto r =
                txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kItemColumns) +
                                    " FROM travel_trip_items WHERE owner_id = $1::uuid AND trip_id = $2::uuid "
                                    " ORDER BY date, time NULLS FIRST, position, id) t",
                                owner,
                                trip);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws NotFound when the trip is not the owner's.
    nlohmann::json add_item(const std::string& owner, const std::string& trip, const ItemInput& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO travel_trip_items (owner_id, trip_id, kind, date, time, title, reference, link, "
                        " note, no_crossing) SELECT $1::uuid, id, $3, $4::date, $5::time, $6, $7, $8, $9, $10 "
                        "FROM travel_trips WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id::text",
                        owner,
                        trip,
                        in.kind,
                        in.date,
                        in.time,
                        in.title,
                        in.reference,
                        in.link,
                        in.note,
                        in.no_crossing);
                    if (r.empty()) {
                        throw NotFound("trip");
                    }
                    return *find_item_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// A full replacement of an item. @throws NotFound.
    nlohmann::json replace_item(const std::string& owner,
                                const std::string& trip,
                                const std::string& id,
                                const ItemInput& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE travel_trip_items SET kind = $4, date = $5::date, time = $6::time, title = $7, "
                        " reference = $8, link = $9, note = $10, no_crossing = $11 "
                        "WHERE owner_id = $1::uuid AND trip_id = $2::uuid AND id = $3::uuid RETURNING id",
                        owner,
                        trip,
                        id,
                        in.kind,
                        in.date,
                        in.time,
                        in.title,
                        in.reference,
                        in.link,
                        in.note,
                        in.no_crossing);
                    if (r.empty()) {
                        throw NotFound("trip_item");
                    }
                    return *find_item_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    void remove_item(const std::string& owner, const std::string& trip, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM travel_trip_items WHERE owner_id = $1::uuid AND trip_id = $2::uuid AND id = $3::uuid "
                "RETURNING id",
                owner,
                trip,
                id);
            if (r.empty()) {
                throw NotFound("trip_item");
            }
            return true;
        });
    }

    // ── links from other modules ───────────────────────────────────────────

    /// Packing: the trip's tasks, done of all.
    std::pair<int, int> packing(const std::string& owner, const std::string& trip) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT count(*) FILTER (WHERE status = 'done'), count(*) FROM task_items "
                "WHERE owner_id = $1::uuid AND trip_id = $2::uuid",
                owner,
                trip);
            return std::pair<int, int>{r[0][0].template as<int>(), r[0][1].template as<int>()};
        });
    }

    /// The trip's linked spends, oldest first: id, date, name, amount, currency.
    nlohmann::json spends(const std::string& owner, const std::string& trip) {
        return spend_rows(owner, trip, "t.trip_id = $2::uuid");
    }

    /// Posted spends of the trip's dates that belong to no other trip.
    nlohmann::json candidate_spends(const std::string& owner, const std::string& trip) {
        return spend_rows(
            owner,
            trip,
            "t.date BETWEEN tr.start_date AND tr.end_date AND (t.trip_id IS NULL OR t.trip_id = $2::uuid)");
    }

    /// Link (or unlink) the owner's posted spends to the trip; another owner's ids change nothing.
    /// @returns how many rows changed. @throws NotFound when the trip is not the owner's.
    int link_spends(const std::string& owner, const std::string& trip, const std::vector<std::string>& ids, bool link) {
        return Database::get().execute_write([&](auto& txn) {
            auto mine =
                txn.exec_params("SELECT 1 FROM travel_trips WHERE owner_id = $1::uuid AND id = $2::uuid", owner, trip);
            if (mine.empty()) {
                throw NotFound("trip");
            }
            std::string list = "{";
            for (const auto& id : ids) {
                if (list.size() > 1) {
                    list += ',';
                }
                list += id;  // checked as uuids by the controller
            }
            list += "}";
            auto r = link ? txn.exec_params(
                                "UPDATE money_transactions SET trip_id = $2::uuid WHERE owner_id = $1::uuid "
                                " AND id = ANY($3::uuid[]) AND type = 'expense' AND status = 'posted' "
                                " AND (trip_id IS NULL OR trip_id = $2::uuid) RETURNING id",
                                owner,
                                trip,
                                list)
                          : txn.exec_params(
                                "UPDATE money_transactions SET trip_id = NULL WHERE owner_id = $1::uuid "
                                " AND id = ANY($3::uuid[]) AND trip_id = $2::uuid RETURNING id",
                                owner,
                                trip,
                                list);
            return static_cast<int>(r.size());
        });
    }

private:
    static constexpr const char* kColumns =
        "id, name, to_char(start_date, 'YYYY-MM-DD') AS start_date, to_char(end_date, 'YYYY-MM-DD') AS end_date, "
        "countries, note";
    static constexpr const char* kItemColumns =
        "id, trip_id, kind, to_char(date, 'YYYY-MM-DD') AS date, to_char(time, 'HH24:MI') AS time, title, reference, "
        "link, note, no_crossing";

    nlohmann::json spend_rows(const std::string& owner, const std::string& trip, const std::string& where) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(s), '[]'::json) FROM ("
                " SELECT t.id, to_char(t.date, 'YYYY-MM-DD') AS date, t.name, t.amount::float8 AS amount, "
                "  a.currency, (t.trip_id IS NOT NULL) AS linked "
                " FROM money_transactions t JOIN money_accounts a ON a.id = t.account_id "
                " JOIN travel_trips tr ON tr.id = $2::uuid AND tr.owner_id = $1::uuid "
                " WHERE t.owner_id = $1::uuid AND t.type = 'expense' AND t.status = 'posted' AND " +
                    where + " ORDER BY t.date, t.created_at) s",
                owner,
                trip);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    template <typename Txn>
    std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM travel_trips WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }

    template <typename Txn>
    std::optional<nlohmann::json> find_item_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kItemColumns) +
                                     " FROM travel_trip_items WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Travel
