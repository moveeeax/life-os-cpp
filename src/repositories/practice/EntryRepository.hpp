/**
 * @file EntryRepository.hpp
 * @brief practice_entries: the incomes and expenses of the practice, each in
 *        its own currency, linked to a client or a deal (an entry of a deal
 *        belongs to the deal's client). The month and year views total per
 *        currency: amounts of two currencies are never added together.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/SqlTime.hpp"
#include "repositories/practice/DealRepository.hpp"
#include "repositories/practice/Errors.hpp"

namespace Repositories::Practice {

class EntryRepository {
public:
    struct Input {
        std::string kind;
        std::string date;
        double amount = 0;
        std::string currency;
        std::optional<std::string> client_id;
        std::optional<std::string> deal_id;
        std::string note;
    };

    /// A partial edit; a link given as null clears it.
    struct Patch {
        std::optional<std::string> kind;
        std::optional<std::string> date;
        std::optional<double> amount;
        std::optional<std::string> currency;
        std::optional<std::optional<std::string>> client_id;
        std::optional<std::optional<std::string>> deal_id;
        std::optional<std::string> note;
    };

    struct Filter {
        std::string kind;    // "" any
        std::string from;    // "" or YYYY-MM-DD, inclusive
        std::string before;  // "" or YYYY-MM-DD, exclusive
        std::string client;  // "" any, or a client id
        std::string deal;    // "" any, or a deal id
        int limit = 50;
        int offset = 0;
    };

    /// The deal's client fills an empty client. @throws NotOfClient, Invariant.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    const auto client = linked_client(txn, owner, in.client_id, in.client_id.has_value(), in.deal_id);
                    auto r = txn.exec_params(
                        "INSERT INTO practice_entries (owner_id, kind, date, amount, currency, client_id, deal_id, "
                        " note) VALUES ($1::uuid, $2, $3::date, $4, $5, $6::uuid, $7::uuid, $8) RETURNING id::text",
                        owner,
                        in.kind,
                        in.date,
                        in.amount,
                        in.currency,
                        client,
                        in.deal_id,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// @throws NotFound, NotOfClient, Invariant.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto cur = txn.exec_params(
                        "SELECT client_id::text, deal_id::text FROM practice_entries "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid FOR UPDATE",
                        owner,
                        id);
                    if (cur.empty()) {
                        throw NotFound("entry");
                    }
                    const auto text = [](const auto& field) {
                        return field.is_null() ? std::optional<std::string>() : field.template as<std::string>();
                    };
                    const auto deal = p.deal_id ? *p.deal_id : text(cur[0][1]);
                    // A deal given anew brings its client unless the edit names one.
                    const bool client_given = p.client_id.has_value();
                    const auto asked = client_given ? *p.client_id : text(cur[0][0]);
                    const auto client = linked_client(txn, owner, asked, client_given || !p.deal_id, deal);
                    txn.exec_params(
                        "UPDATE practice_entries SET kind = COALESCE($3, kind), date = COALESCE($4::date, date), "
                        " amount = COALESCE($5, amount), currency = COALESCE($6, currency), client_id = $7::uuid, "
                        " deal_id = $8::uuid, note = COALESCE($9, note) WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        id,
                        p.kind,
                        p.date,
                        p.amount,
                        p.currency,
                        client,
                        deal,
                        p.note);
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            if (txn.exec_params("DELETE FROM practice_entries WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                                owner,
                                id)
                    .empty()) {
                throw NotFound("entry");
            }
            return true;
        });
    }

    /// {data: the latest day first, total}.
    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            const std::string where =
                " WHERE e.owner_id = $1::uuid AND ($2 = '' OR e.kind = $2) "
                " AND ($3 = '' OR e.date >= NULLIF($3, '')::date) AND ($4 = '' OR e.date < NULLIF($4, '')::date) "
                " AND ($5 = '' OR e.client_id = NULLIF($5, '')::uuid) AND ($6 = '' OR e.deal_id = NULLIF($6, "
                "'')::uuid) "
                " AND $7::int >= 0 AND $8::int >= 0 ";
            auto total = txn.exec_params("SELECT count(*) FROM practice_entries e" + where,
                                         owner,
                                         f.kind,
                                         f.from,
                                         f.before,
                                         f.client,
                                         f.deal,
                                         f.limit,
                                         f.offset);
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM practice_entries e" + where +
                                         " ORDER BY e.date DESC, e.created_at DESC LIMIT $7 OFFSET $8) t",
                                     owner,
                                     f.kind,
                                     f.from,
                                     f.before,
                                     f.client,
                                     f.deal,
                                     f.limit,
                                     f.offset);
            return nlohmann::json{{"data", nlohmann::json::parse(r[0][0].template as<std::string>())},
                                  {"total", total[0][0].template as<long>()}};
        });
    }

    /// The months of [from, before) that have entries, the latest first, each
    /// {month, entries, totals}, and the totals of the whole range.
    nlohmann::json months(const std::string& owner, const std::string& from, const std::string& before) {
        return Database::get().execute_read([&](auto& txn) {
            auto m = txn.exec_params(
                "SELECT COALESCE(json_agg(m ORDER BY m.month DESC), '[]'::json) FROM (SELECT y.month, "
                " SUM(y.entries)::int AS entries, json_agg(json_build_object('currency', y.currency, 'income', "
                " y.income, 'expense', y.expense, 'net', y.net, 'entries', y.entries) ORDER BY y.currency) AS totals "
                " FROM (SELECT to_char(i.date, 'YYYY-MM') AS month, " +
                    std::string(kTotalColumns) +
                    " FROM practice_entries i WHERE i.owner_id = $1::uuid AND i.date >= $2::date AND i.date < $3::date "
                    " GROUP BY to_char(i.date, 'YYYY-MM'), i.currency) y GROUP BY y.month) m",
                owner,
                from,
                before);
            return nlohmann::json{{"data", nlohmann::json::parse(m[0][0].template as<std::string>())},
                                  {"totals", totals_in(txn, owner, from, before)}};
        });
    }

    /// One month [from, before): totals per currency, per client and currency, and every entry.
    nlohmann::json month(const std::string& owner, const std::string& from, const std::string& before) {
        return Database::get().execute_read([&](auto& txn) {
            auto c = txn.exec_params(
                "SELECT COALESCE(json_agg(x ORDER BY x.client_name NULLS LAST, x.currency), '[]'::json) FROM ("
                "SELECT i.client_id, (SELECT c.name FROM practice_clients c WHERE c.id = i.client_id) AS "
                "client_name, " +
                    std::string(kTotalColumns) +
                    " FROM practice_entries i WHERE i.owner_id = $1::uuid AND i.date >= $2::date AND i.date < $3::date "
                    " GROUP BY i.client_id, i.currency) x",
                owner,
                from,
                before);
            auto e = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                         " FROM practice_entries e WHERE e.owner_id = $1::uuid AND e.date >= $2::date "
                                         " AND e.date < $3::date ORDER BY e.date DESC, e.created_at DESC) t",
                                     owner,
                                     from,
                                     before);
            return nlohmann::json{{"totals", totals_in(txn, owner, from, before)},
                                  {"by_client", nlohmann::json::parse(c[0][0].template as<std::string>())},
                                  {"entries", nlohmann::json::parse(e[0][0].template as<std::string>())}};
        });
    }

private:
    /// The total columns of one currency over the entries aliased i: income, expense, net and the count.
    static constexpr const char* kTotalColumns =
        "i.currency, COALESCE(SUM(i.amount) FILTER (WHERE i.kind = 'income'), 0) AS income, "
        " COALESCE(SUM(i.amount) FILTER (WHERE i.kind = 'expense'), 0) AS expense, "
        " COALESCE(SUM(i.amount) FILTER (WHERE i.kind = 'income'), 0) "
        "   - COALESCE(SUM(i.amount) FILTER (WHERE i.kind = 'expense'), 0) AS net, count(*)::int AS entries";

    static constexpr const char* kColumns =
        "e.id, e.kind, to_char(e.date, 'YYYY-MM-DD') AS date, e.amount, e.currency, e.client_id, "
        " (SELECT c.name FROM practice_clients c WHERE c.id = e.client_id) AS client_name, e.deal_id, "
        " (SELECT d.title FROM practice_deals d WHERE d.id = e.deal_id) AS deal_title, e.note, "
        " " SQL_ISO_UTC("e.created_at") " AS created_at, "
        " " SQL_ISO_UTC("e.updated_at") " AS updated_at";

    template <typename Txn>
    static nlohmann::json totals_in(Txn& txn,
                                    const std::string& owner,
                                    const std::string& from,
                                    const std::string& before) {
        auto r = txn.exec_params("SELECT COALESCE(json_agg(y ORDER BY y.currency), '[]'::json) FROM (SELECT " +
                                     std::string(kTotalColumns) +
                                     " FROM practice_entries i WHERE i.owner_id = $1::uuid AND i.date >= $2::date "
                                     " AND i.date < $3::date GROUP BY i.currency) y",
                                 owner,
                                 from,
                                 before);
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }

    /// The client an entry keeps: the deal's when it has one. @p asked counts
    /// only when @p strict (the caller named a client); a named client other
    /// than the deal's is refused. @throws NotOfClient.
    template <typename Txn>
    static std::optional<std::string> linked_client(Txn& txn,
                                                    const std::string& owner,
                                                    const std::optional<std::string>& asked,
                                                    bool strict,
                                                    const std::optional<std::string>& deal) {
        if (!deal) {
            return asked;
        }
        const auto of_deal = DealRepository::client_in(txn, owner, *deal);
        if (!of_deal) {
            throw Invariant("the deal is not there");
        }
        if (strict && asked && *asked != *of_deal) {
            throw NotOfClient();
        }
        return of_deal;
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM practice_entries e WHERE e.owner_id = $1::uuid AND e.id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Practice
