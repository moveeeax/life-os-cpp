/**
 * @file TransferRepository.hpp
 * @brief Transfers between accounts and currency exchanges (money_transfers):
 *        one row, the amount sent in the source currency and the amount
 *        received in the target's (invariant 3). The cost rate is read, not
 *        stored.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class TransferRepository {
public:
    struct Input {
        std::string date;
        std::string from_account_id;
        std::string to_account_id;
        double amount_sent = 0;
        std::optional<double> amount_received;
        std::optional<double> fee;
        std::string name;
        std::string note;
        std::optional<std::string> external_id;
    };

    struct Patch {
        std::optional<std::string> date;
        std::optional<double> amount_sent;
        std::optional<std::optional<double>> amount_received;
        std::optional<std::optional<double>> fee;
        std::optional<std::string> name, note;
    };

    struct Filter {
        std::optional<std::string> from, to, account_id;
        long limit = 50;
        long offset = 0;
    };

    /// @throws Invariant, InvalidDate.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_pair(txn, owner, in.from_account_id, in.to_account_id, in.amount_received);
                    if (in.amount_sent <= 0 || (in.amount_received && *in.amount_received <= 0) ||
                        (in.fee && *in.fee < 0)) {
                        throw Invariant("amounts must be above 0 and the fee 0 or more");
                    }
                    auto r = txn.exec_params(
                        "INSERT INTO money_transfers (owner_id, date, from_account_id, to_account_id, amount_sent, "
                        " amount_received, fee, name, note, external_id) "
                        "VALUES ($1::uuid, $2::date, $3::uuid, $4::uuid, $5, $6, $7, $8, $9, $10) RETURNING id::text",
                        owner,
                        in.date,
                        in.from_account_id,
                        in.to_account_id,
                        in.amount_sent,
                        in.amount_received,
                        in.fee,
                        in.name,
                        in.note,
                        in.external_id);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (" + kSelect +
                    " WHERE x.owner_id = $1::uuid AND ($2::date IS NULL OR x.date >= $2::date) "
                    " AND ($3::date IS NULL OR x.date <= $3::date) "
                    " AND ($4::uuid IS NULL OR x.from_account_id = $4::uuid OR x.to_account_id = $4::uuid) "
                    " ORDER BY x.date DESC, x.created_at DESC LIMIT $5 OFFSET $6) t",
                owner,
                f.from,
                f.to,
                f.account_id,
                f.limit,
                f.offset);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The accounts of a transfer do not change: make a new one. @throws NotFound, Invariant, InvalidDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    const std::optional<nlohmann::json> row = find_in(txn, owner, id);
                    if (!row.has_value()) {
                        throw NotFound("money_transfer");
                    }
                    const std::optional<double> received =
                        p.amount_received.has_value()
                            ? *p.amount_received
                            : ((*row)["amount_received"].is_null() ? std::optional<double>()
                                                                   : (*row)["amount_received"].get<double>());
                    check_pair(txn,
                               owner,
                               (*row)["from_account_id"].get<std::string>(),
                               (*row)["to_account_id"].get<std::string>(),
                               received);
                    if ((p.amount_sent && *p.amount_sent <= 0) || (received && *received <= 0) ||
                        (p.fee.has_value() && *p.fee && **p.fee < 0)) {
                        throw Invariant("amounts must be above 0 and the fee 0 or more");
                    }
                    txn.exec_params(
                        "UPDATE money_transfers SET date = COALESCE($3::date, date), "
                        " amount_sent = COALESCE($4, amount_sent), "
                        " amount_received = CASE WHEN $5::boolean THEN $6 ELSE amount_received END, "
                        " fee = CASE WHEN $7::boolean THEN $8 ELSE fee END, "
                        " name = COALESCE($9, name), note = COALESCE($10, note), updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        id,
                        p.date,
                        p.amount_sent,
                        p.amount_received.has_value(),
                        p.amount_received.has_value() ? *p.amount_received : std::optional<double>(),
                        p.fee.has_value(),
                        p.fee.has_value() ? *p.fee : std::optional<double>(),
                        p.name,
                        p.note);
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM money_transfers WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("money_transfer");
            }
            return true;
        });
    }

private:
    inline static const std::string kSelect =
        "SELECT x.id, to_char(x.date, 'YYYY-MM-DD') AS date, x.from_account_id, x.to_account_id, "
        " f.currency AS from_currency, g.currency AS to_currency, x.amount_sent, x.amount_received, x.fee, "
        " CASE WHEN x.amount_received IS NULL OR x.amount_received = 0 THEN NULL "
        "      ELSE x.amount_sent / x.amount_received END AS cost_rate, "
        " x.name, x.note, x.external_id, "
        " to_char(x.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(x.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at "
        "FROM money_transfers x JOIN money_accounts f ON f.id = x.from_account_id "
        " JOIN money_accounts g ON g.id = x.to_account_id";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (" + kSelect + " WHERE x.owner_id = $1::uuid AND x.id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }

    /// Both accounts the owner's and different; received given iff the currencies differ.
    template <typename Txn>
    static void check_pair(Txn& txn,
                           const std::string& owner,
                           const std::string& from,
                           const std::string& to,
                           const std::optional<double>& received) {
        if (from == to) {
            throw Invariant("a transfer needs two different accounts");
        }
        const auto from_cur = AccountRepository::currency_in(txn, owner, from);
        const auto to_cur = AccountRepository::currency_in(txn, owner, to);
        if (!from_cur || !to_cur) {
            throw Invariant("both accounts must be yours");
        }
        if (*from_cur == *to_cur && received.has_value()) {
            throw Invariant("same currency: leave the amount received empty");
        }
        if (*from_cur != *to_cur && !received.has_value()) {
            throw Invariant("different currencies: the amount received is needed");
        }
    }
};

}  // namespace Repositories::Money
