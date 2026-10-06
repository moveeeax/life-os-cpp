/**
 * @file AccountRepository.hpp
 * @brief Accounts and cash of a user (money_accounts). The balance is never
 *        stored: it is the opening balance plus everything the ledger and the
 *        transfers did to the account, computed in SQL on every read.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class AccountRepository {
public:
    struct Input {
        std::string name;
        std::string bank;
        std::string kind = "card";
        std::string currency;
        std::string last4;
        double opening_balance = 0;
        std::optional<std::string> opening_date;
        int position = 0;
    };

    struct Patch {
        std::optional<std::string> name, bank, kind, last4;
        std::optional<double> opening_balance;
        std::optional<std::optional<std::string>> opening_date;
        std::optional<bool> archived;
        std::optional<int> position;
    };

    /// Rows with their computed balance and the date of the last activity.
    nlohmann::json list(const std::string& owner, bool include_archived) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (" + kSelect +
                                         " WHERE a.owner_id = $1::uuid AND ($2::boolean OR NOT a.archived) "
                                         " ORDER BY a.currency, a.position, a.name) t",
                                     owner,
                                     include_archived);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    /// @throws Invariant when the currency is not the owner's.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO money_accounts (owner_id, name, bank, kind, currency, last4, opening_balance, "
                        " opening_date, position) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8::date, $9) RETURNING "
                        "id::text",
                        owner,
                        in.name,
                        in.bank,
                        in.kind,
                        in.currency,
                        in.last4,
                        in.opening_balance,
                        in.opening_date,
                        in.position);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// The currency of an account cannot change: its rows are in it. @throws NotFound, Invariant.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE money_accounts SET name = COALESCE($3, name), bank = COALESCE($4, bank), "
                        " kind = COALESCE($5, kind), last4 = COALESCE($6, last4), "
                        " opening_balance = COALESCE($7, opening_balance), "
                        " opening_date = CASE WHEN $8::boolean THEN $9::date ELSE opening_date END, "
                        " archived = COALESCE($10, archived), position = COALESCE($11, position), updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.name,
                        p.bank,
                        p.kind,
                        p.last4,
                        p.opening_balance,
                        p.opening_date.has_value(),
                        p.opening_date.has_value() ? *p.opening_date : std::optional<std::string>(),
                        p.archived,
                        p.position);
                    if (r.empty()) {
                        throw NotFound("money_account");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Deletes an account without rows; archives one that has any. @throws NotFound.
    std::string remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) -> std::string {
            auto own =
                txn.exec_params("SELECT 1 FROM money_accounts WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id);
            if (own.empty()) {
                throw NotFound("money_account");
            }
            auto used = txn.exec_params(
                "SELECT 1 FROM money_transactions WHERE account_id = $1::uuid "
                "UNION ALL SELECT 1 FROM money_transfers WHERE from_account_id = $1::uuid OR to_account_id = $1::uuid "
                "LIMIT 1",
                id);
            if (!used.empty()) {
                txn.exec_params("UPDATE money_accounts SET archived = true, updated_at = now() WHERE id = $1::uuid",
                                id);
                return "archived";
            }
            txn.exec_params("DELETE FROM money_accounts WHERE id = $1::uuid", id);
            return "deleted";
        });
    }

    /// The code of an owner's account, or nullopt when it is not theirs.
    template <typename Txn>
    static std::optional<std::string> currency_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT currency FROM money_accounts WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id);
        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    }

private:
    // The balance: opening + income − expense ± adjustments (income sign for a
    // positive adjustment means the bank took more, so it is subtracted)
    // + transfers in (received, or sent when received is null) − transfers out
    // (sent plus the fee). Pending rows are not money yet.
    inline static const std::string kSelect =
        "SELECT a.id, a.name, a.bank, a.kind, a.currency, a.last4, a.opening_balance, "
        " to_char(a.opening_date, 'YYYY-MM-DD') AS opening_date, a.archived, a.position, "
        " a.opening_balance "
        "  + COALESCE((SELECT SUM(CASE t.type WHEN 'income' THEN t.amount ELSE -t.amount END) "
        "              FROM money_transactions t WHERE t.account_id = a.id AND t.status = 'posted'), 0) "
        "  + COALESCE((SELECT SUM(COALESCE(x.amount_received, x.amount_sent)) FROM money_transfers x "
        "              WHERE x.to_account_id = a.id), 0) "
        "  - COALESCE((SELECT SUM(x.amount_sent + COALESCE(x.fee, 0)) FROM money_transfers x "
        "              WHERE x.from_account_id = a.id), 0) AS balance, "
        " (SELECT to_char(MAX(d), 'YYYY-MM-DD') FROM (SELECT MAX(t.date) AS d FROM money_transactions t "
        "   WHERE t.account_id = a.id AND t.status = 'posted' UNION ALL SELECT MAX(x.date) FROM money_transfers x "
        "   WHERE x.from_account_id = a.id OR x.to_account_id = a.id) m) AS last_activity, "
        " to_char(a.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(a.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at "
        "FROM money_accounts a";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (" + kSelect + " WHERE a.owner_id = $1::uuid AND a.id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Money
