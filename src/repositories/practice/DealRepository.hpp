/**
 * @file DealRepository.hpp
 * @brief practice_deals: a piece of work for a client, moving through the
 *        stages lead, negotiation, active, done or lost. A deal answers with
 *        its open tasks (the next steps, task_items.deal_id) and what it has
 *        brought in, per currency.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/practice/Errors.hpp"

namespace Repositories::Practice {

class DealRepository {
public:
    struct Input {
        std::string client_id;
        std::string title;
        std::string stage = "lead";
        std::optional<double> value;
        std::optional<std::string> currency;
        std::optional<std::string> start_date;
        std::optional<std::string> end_date;
        std::string note;
    };

    /// A partial edit; outer optional: given, inner: null clears.
    struct Patch {
        std::optional<std::string> client_id;
        std::optional<std::string> title;
        std::optional<std::string> stage;
        std::optional<std::optional<double>> value;
        std::optional<std::optional<std::string>> currency;
        std::optional<std::optional<std::string>> start_date;
        std::optional<std::optional<std::string>> end_date;
        std::optional<std::string> note;
    };

    struct Filter {
        std::string stage;   // "" any
        std::string client;  // "" any, or a client id
    };

    /// @throws Invariant.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO practice_deals (owner_id, client_id, title, stage, value, currency, start_date, "
                        " end_date, note) "
                        "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7::date, $8::date, $9) RETURNING id::text",
                        owner,
                        in.client_id,
                        in.title,
                        in.stage,
                        in.value,
                        in.currency,
                        in.start_date,
                        in.end_date,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// The client of the owner's deal @p id, or nullopt when the deal is not the owner's.
    template <typename Txn>
    static std::optional<std::string> client_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT client_id::text FROM practice_deals WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id);
        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    }

    std::optional<std::string> client_of(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return client_in(txn, owner, id); });
    }

    /// A new client moves the deal's entries with it. @throws NotFound, Invariant.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE practice_deals SET client_id = COALESCE($3::uuid, client_id), "
                        " title = COALESCE($4, title), stage = COALESCE($5, stage), "
                        " value = CASE WHEN $6::boolean THEN $7 ELSE value END, "
                        " currency = CASE WHEN $8::boolean THEN $9 ELSE currency END, "
                        " start_date = CASE WHEN $10::boolean THEN $11::date ELSE start_date END, "
                        " end_date = CASE WHEN $12::boolean THEN $13::date ELSE end_date END, "
                        " note = COALESCE($14, note) "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.client_id,
                        p.title,
                        p.stage,
                        p.value.has_value(),
                        p.value.has_value() ? *p.value : std::optional<double>(),
                        p.currency.has_value(),
                        p.currency.has_value() ? *p.currency : std::optional<std::string>(),
                        p.start_date.has_value(),
                        p.start_date.has_value() ? *p.start_date : std::optional<std::string>(),
                        p.end_date.has_value(),
                        p.end_date.has_value() ? *p.end_date : std::optional<std::string>(),
                        p.note);
                    if (r.empty()) {
                        throw NotFound("deal");
                    }
                    if (p.client_id) {
                        txn.exec_params(
                            "UPDATE practice_entries SET client_id = $3::uuid "
                            "WHERE owner_id = $1::uuid AND deal_id = $2::uuid AND client_id IS DISTINCT FROM $3::uuid",
                            owner,
                            id,
                            *p.client_id);
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Its entries and tasks stay, unlinked. @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            if (txn.exec_params(
                       "DELETE FROM practice_deals WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id)
                    .empty()) {
                throw NotFound("deal");
            }
            return true;
        });
    }

    /// The owner's deals, open stages first (kStages order), the latest touched first within a stage.
    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                    " FROM practice_deals d WHERE d.owner_id = $1::uuid AND ($2 = '' OR d.stage = $2) "
                    " AND ($3 = '' OR d.client_id = NULLIF($3, '')::uuid) "
                    " ORDER BY array_position(ARRAY['lead', 'negotiation', 'active', 'done', 'lost'], d.stage), "
                    " d.updated_at DESC) t",
                owner,
                f.stage,
                f.client);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

private:
    static constexpr const char* kColumns =
        "d.id, d.client_id, (SELECT c.name FROM practice_clients c WHERE c.id = d.client_id) AS client_name, "
        " d.title, d.stage, d.value, d.currency, to_char(d.start_date, 'YYYY-MM-DD') AS start_date, "
        " to_char(d.end_date, 'YYYY-MM-DD') AS end_date, d.note, "
        " (SELECT count(*) FROM task_items t WHERE t.deal_id = d.id AND t.status <> 'done')::int AS open_tasks, "
        " (SELECT COALESCE(json_agg(x ORDER BY x.currency), '[]'::json) FROM (SELECT e.currency, "
        "   SUM(e.amount) AS amount FROM practice_entries e WHERE e.deal_id = d.id AND e.kind = 'income' "
        "   GROUP BY e.currency) x) AS received, "
        " " SQL_ISO_UTC("d.created_at") " AS created_at, "
        " " SQL_ISO_UTC("d.updated_at") " AS updated_at";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM practice_deals d WHERE d.owner_id = $1::uuid AND d.id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Practice
