/**
 * @file TransactionRepository.hpp
 * @brief The ledger (money_transactions): incomes, expenses and the bank's
 *        adjustments, each on one account in that account's currency. The
 *        repository checks what the CHECKs cannot: that the account, the
 *        category and the adjusted row are the owner's, that an adjustment
 *        sits on its original's account, and that a category's kind matches
 *        the type. A posted row with a merchant teaches the merchant memory.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "money/Merchant.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class TransactionRepository {
public:
    struct Input {
        std::string type = "expense";
        std::string date;
        std::optional<std::string> time;
        std::string account_id;
        double amount = 0;
        std::optional<std::string> category_id;
        std::string merchant;
        std::string name;
        std::optional<double> receipt_amount;
        std::optional<std::string> receipt_currency;
        std::string fx_note;
        std::optional<std::string> adjusts_id;
        std::string trip;
        std::string note;
        std::string source = "manual";
        std::string status = "posted";
        std::optional<std::string> external_id;
    };

    struct Patch {
        std::optional<std::string> date;
        std::optional<std::optional<std::string>> time;
        std::optional<std::string> account_id;
        std::optional<double> amount;
        std::optional<std::string> category_id;
        std::optional<std::string> merchant, name;
        std::optional<std::optional<double>> receipt_amount;
        std::optional<std::optional<std::string>> receipt_currency;
        std::optional<std::string> fx_note, trip, note;
    };

    struct Filter {
        std::optional<std::string> from, to, account_id, category_id, currency, type, status, trip;
        std::string q;
        long limit = 50;
        long offset = 0;
    };

    struct Page {
        nlohmann::json rows = nlohmann::json::array();
        long total = 0;
    };

    /// @throws Invariant, InvalidDate.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    const std::string id = insert_in(txn, owner, in);
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// All rows or none. @throws Invariant, InvalidDate.
    nlohmann::json create_many(const std::string& owner, const std::vector<Input>& inputs) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    nlohmann::json out = nlohmann::json::array();
                    for (const auto& in : inputs) {
                        out.push_back(*find_in(txn, owner, insert_in(txn, owner, in)));
                    }
                    return out;
                });
            },
            &detail::translate);
    }

    /// The row with its currency, its adjustments and the final amount.
    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    Page list(const std::string& owner, const Filter& f) {
        Page out;
        Database::get().execute_read([&](auto& txn) {
            const std::string where =
                "t.owner_id = $1::uuid "
                "AND ($2::date IS NULL OR t.date >= $2::date) AND ($3::date IS NULL OR t.date <= $3::date) "
                "AND ($4::uuid IS NULL OR t.account_id = $4::uuid) AND ($5::uuid IS NULL OR t.category_id = $5::uuid) "
                "AND ($6::text IS NULL OR a.currency = $6) AND ($7::text IS NULL OR t.type = $7) "
                "AND ($8::text IS NULL OR t.status = $8) AND ($9::text IS NULL OR t.trip = $9) "
                "AND ($10 = '' OR t.name ILIKE '%' || $10 || '%' ESCAPE '\\' OR t.merchant ILIKE '%' || $10 || '%' "
                "ESCAPE '\\')";
            const std::string q = escape_like(f.q);
            auto rows =
                txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (" + kSelect + " WHERE " + where +
                                    " ORDER BY t.date DESC, t.time DESC NULLS LAST, t.created_at DESC, t.id "
                                    " LIMIT $11 OFFSET $12) t",
                                owner,
                                f.from,
                                f.to,
                                f.account_id,
                                f.category_id,
                                f.currency,
                                f.type,
                                f.status,
                                f.trip,
                                q,
                                f.limit,
                                f.offset);
            out.rows = nlohmann::json::parse(rows[0][0].template as<std::string>());
            auto total = txn.exec_params(
                "SELECT COUNT(*) FROM money_transactions t JOIN money_accounts a ON a.id = t.account_id WHERE " + where,
                owner,
                f.from,
                f.to,
                f.account_id,
                f.category_id,
                f.currency,
                f.type,
                f.status,
                f.trip,
                q);
            out.total = total[0][0].template as<long>();
            return true;
        });
        return out;
    }

    /// An adjustment may change its amount and note only. @throws NotFound, Invariant, InvalidDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    const std::optional<nlohmann::json> row = find_in(txn, owner, id);
                    if (!row.has_value()) {
                        throw NotFound("money_transaction");
                    }
                    const std::string type = (*row)["type"].get<std::string>();
                    const std::string account = p.account_id.value_or((*row)["account_id"].get<std::string>());
                    if (type == "fx_adjustment" &&
                        (p.date || p.time || p.account_id || p.category_id || p.merchant || p.name ||
                         p.receipt_amount || p.receipt_currency || p.fx_note || p.trip)) {
                        throw Invariant("an adjustment may change only its amount and note");
                    }
                    if (p.account_id.has_value()) {
                        check_account(txn, owner, account);
                        if (AccountRepository::currency_in(txn, owner, account) !=
                            (*row)["currency"].get<std::string>()) {
                            throw Invariant(
                                "the amount is in the account's currency: move only to an account in "
                                "the same currency");
                        }
                        if (!(*row)["adjustments"].empty()) {
                            throw Invariant("a row with adjustments stays on its account");
                        }
                    }
                    if (p.category_id.has_value()) {
                        check_category(txn, owner, type, *p.category_id);
                    }
                    if (p.amount.has_value() && type != "fx_adjustment" && *p.amount <= 0) {
                        throw Invariant("amount must be above 0");
                    }
                    if (p.amount.has_value() && type == "fx_adjustment" && *p.amount == 0) {
                        throw Invariant("an adjustment of 0 says nothing");
                    }
                    const std::string merchant = p.merchant.value_or((*row)["merchant"].get<std::string>());
                    txn.exec_params(
                        "UPDATE money_transactions SET date = COALESCE($3::date, date), "
                        " time = CASE WHEN $4::boolean THEN $5::time ELSE time END, "
                        " account_id = COALESCE($6::uuid, account_id), amount = COALESCE($7, amount), "
                        " category_id = COALESCE($8::uuid, category_id), merchant = $9, merchant_key = $10, "
                        " name = COALESCE($11, name), "
                        " receipt_amount = CASE WHEN $12::boolean THEN $13 ELSE receipt_amount END, "
                        " receipt_currency = CASE WHEN $14::boolean THEN $15 ELSE receipt_currency END, "
                        " fx_note = COALESCE($16, fx_note), trip = COALESCE($17, trip), note = COALESCE($18, note), "
                        " updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        id,
                        p.date,
                        p.time.has_value(),
                        p.time.has_value() ? *p.time : std::optional<std::string>(),
                        p.account_id,
                        p.amount,
                        p.category_id,
                        merchant,
                        ::Money::merchant_key(merchant),
                        p.name,
                        p.receipt_amount.has_value(),
                        p.receipt_amount.has_value() ? *p.receipt_amount : std::optional<double>(),
                        p.receipt_currency.has_value(),
                        p.receipt_currency.has_value() ? *p.receipt_currency : std::optional<std::string>(),
                        p.fx_note,
                        p.trip,
                        p.note);
                    const auto updated = *find_in(txn, owner, id);
                    // A new merchant or category is something to learn; a note edit is not a visit.
                    if (updated["status"] == "posted" && (p.merchant.has_value() || p.category_id.has_value())) {
                        remember_in(txn, owner, updated);
                    }
                    return updated;
                });
            },
            &detail::translate);
    }

    /// Deletes the row and, through the FK, its adjustments. @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM money_transactions WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("money_transaction");
            }
            return true;
        });
    }

    /// pending -> posted; the merchant memory learns from it now. @throws NotFound.
    nlohmann::json confirm(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE money_transactions SET status = 'posted', updated_at = now() "
                "WHERE owner_id = $1::uuid AND id = $2::uuid AND status = 'pending' RETURNING id",
                owner,
                id);
            if (r.empty()) {
                throw NotFound("money_transaction");
            }
            const auto row = *find_in(txn, owner, id);
            remember_in(txn, owner, row);
            return row;
        });
    }

    /// Pending rows, each with `possible_duplicate`: a posted row of the same
    /// account and amount dated within a day.
    nlohmann::json inbox(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + kColumns +
                    ", EXISTS (SELECT 1 FROM money_transactions d WHERE d.owner_id = t.owner_id AND d.status = "
                    "'posted' "
                    "   AND d.account_id = t.account_id AND d.amount = t.amount AND d.type = t.type "
                    "   AND d.date BETWEEN t.date - 1 AND t.date + 1) AS possible_duplicate" +
                    kFrom +
                    " WHERE t.owner_id = $1::uuid AND t.status = 'pending' ORDER BY t.date DESC, t.created_at DESC) t",
                owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    std::optional<nlohmann::json> by_external_id(const std::string& owner, const std::string& external_id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT id::text FROM money_transactions WHERE owner_id = $1::uuid AND external_id = $2",
                owner,
                external_id);
            if (r.empty()) {
                return std::nullopt;
            }
            return find_in(txn, owner, r[0][0].template as<std::string>());
        });
    }

    /// The rows a report needs: date, currency, type, category, flexibility, merchant, amount.
    nlohmann::json report_rows(const std::string& owner, const std::string& from, const std::string& to) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT to_char(t.date, 'YYYY-MM-DD') AS date, "
                " a.currency, t.type, t.category_id::text AS category_id, COALESCE(c.kind, '') AS category_kind, "
                " COALESCE(c.flexibility, 'variable') AS flexibility, t.merchant_key, t.amount "
                " FROM money_transactions t JOIN money_accounts a ON a.id = t.account_id "
                " LEFT JOIN money_categories c ON c.id = t.category_id "
                " WHERE t.owner_id = $1::uuid AND t.status = 'posted' AND t.date BETWEEN $2::date AND $3::date "
                " ORDER BY t.date, t.created_at, t.id) t",
                owner,
                from,
                to);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Merchant keys of posted rows dated before `date`.
    std::vector<std::string> merchants_before(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            std::vector<std::string> out;
            auto r = txn.exec_params(
                "SELECT DISTINCT merchant_key FROM money_transactions WHERE owner_id = $1::uuid AND status = 'posted' "
                " AND merchant_key <> '' AND date < $2::date",
                owner,
                date);
            for (const auto& row : r) {
                out.push_back(row[0].template as<std::string>());
            }
            return out;
        });
    }

private:
    // The row as the API shows it: with the account's currency, the final
    // amount (itself plus its adjustments) and the adjustments.
    inline static const std::string kColumns =
        "t.id, t.type, to_char(t.date, 'YYYY-MM-DD') AS date, to_char(t.time, 'HH24:MI') AS time, "
        " t.account_id, a.currency, t.amount, t.category_id, t.merchant, t.merchant_key, t.name, "
        " t.receipt_amount, t.receipt_currency, t.fx_note, t.adjusts_id, t.trip, t.note, t.source, t.status, "
        " t.external_id, "
        " t.amount + COALESCE((SELECT SUM(j.amount) FROM money_transactions j WHERE j.adjusts_id = t.id), 0) "
        "   AS final_amount, "
        " COALESCE((SELECT json_agg(json_build_object('id', j.id, 'date', to_char(j.date, 'YYYY-MM-DD'), "
        "   'amount', j.amount, 'note', j.note) ORDER BY j.date, j.created_at) "
        "   FROM money_transactions j WHERE j.adjusts_id = t.id), '[]'::json) AS adjustments, "
        " to_char(t.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        " to_char(t.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";
    inline static const std::string kFrom = " FROM money_transactions t JOIN money_accounts a ON a.id = t.account_id";
    inline static const std::string kSelect = "SELECT " + kColumns + kFrom;

    static std::string escape_like(const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '%' || c == '_' || c == '\\') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out;
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params(
            "SELECT row_to_json(t) FROM (" + kSelect + " WHERE t.owner_id = $1::uuid AND t.id = $2::uuid) t",
            owner,
            id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }

    template <typename Txn>
    static void check_account(Txn& txn, const std::string& owner, const std::string& account_id) {
        if (!AccountRepository::currency_in(txn, owner, account_id).has_value()) {
            throw Invariant("the account is not yours");
        }
    }

    template <typename Txn>
    static void check_category(Txn& txn, const std::string& owner, const std::string& type, const std::string& id) {
        const auto kind = CategoryRepository::kind_in(txn, owner, id);
        if (!kind.has_value()) {
            throw Invariant("the category is not yours");
        }
        if ((type == "expense" && *kind != "expense") || (type == "income" && *kind != "income")) {
            throw Invariant("an " + type + " needs a category of kind " + type);
        }
    }

    template <typename Txn>
    static std::string insert_in(Txn& txn, const std::string& owner, const Input& in) {
        check_account(txn, owner, in.account_id);
        if (in.type == "fx_adjustment") {
            if (!in.adjusts_id.has_value()) {
                throw Invariant("an adjustment needs the row it adjusts");
            }
            auto orig = txn.exec_params(
                "SELECT account_id::text, type FROM money_transactions WHERE owner_id = $1::uuid AND id = $2::uuid",
                owner,
                *in.adjusts_id);
            if (orig.empty()) {
                throw Invariant("the adjusted row is not yours");
            }
            if (orig[0][1].template as<std::string>() == "fx_adjustment") {
                throw Invariant("an adjustment cannot adjust an adjustment");
            }
            if (orig[0][0].template as<std::string>() != in.account_id) {
                throw Invariant("an adjustment sits on the account of its original");
            }
            if (in.amount == 0) {
                throw Invariant("an adjustment of 0 says nothing");
            }
        } else {
            if (in.adjusts_id.has_value()) {
                throw Invariant("only an adjustment points at another row");
            }
            if (!in.category_id.has_value()) {
                throw Invariant("an " + in.type + " needs a category");
            }
            check_category(txn, owner, in.type, *in.category_id);
            if (in.amount <= 0) {
                throw Invariant("amount must be above 0");
            }
        }
        auto r = txn.exec_params(
            "INSERT INTO money_transactions (owner_id, type, date, time, account_id, amount, category_id, merchant, "
            " merchant_key, name, receipt_amount, receipt_currency, fx_note, adjusts_id, trip, note, source, status, "
            " external_id) VALUES ($1::uuid, $2, $3::date, $4::time, $5::uuid, $6, $7::uuid, $8, $9, $10, $11, $12, "
            "$13, "
            " $14::uuid, $15, $16, $17, $18, $19) RETURNING id::text",
            owner,
            in.type,
            in.date,
            in.time,
            in.account_id,
            in.amount,
            in.category_id,
            in.merchant,
            ::Money::merchant_key(in.merchant),
            in.name,
            in.receipt_amount,
            in.receipt_currency,
            in.fx_note,
            in.adjusts_id,
            in.trip,
            in.note,
            in.source,
            in.status,
            in.external_id);
        const std::string id = r[0][0].template as<std::string>();
        if (in.status == "posted") {
            remember_in(txn, owner, *find_in(txn, owner, id));
        }
        return id;
    }

    /// The merchant memory: the category of the last posted row wins, the count grows.
    template <typename Txn>
    static void remember_in(Txn& txn, const std::string& owner, const nlohmann::json& row) {
        const std::string key = row.value("merchant_key", std::string());
        if (key.empty() || row["type"] == "fx_adjustment") {
            return;
        }
        txn.exec_params(
            "INSERT INTO money_merchants (owner_id, merchant_key, display_name, category_id, account_id, times, "
            "last_seen) "
            "VALUES ($1::uuid, $2, $3, $4::uuid, $5::uuid, 1, $6::date) "
            "ON CONFLICT (owner_id, merchant_key) DO UPDATE SET display_name = EXCLUDED.display_name, "
            " category_id = COALESCE(EXCLUDED.category_id, money_merchants.category_id), "
            " account_id = EXCLUDED.account_id, times = money_merchants.times + 1, "
            " last_seen = GREATEST(money_merchants.last_seen, EXCLUDED.last_seen)",
            owner,
            key,
            row["merchant"].get<std::string>(),
            row["category_id"].is_null() ? std::optional<std::string>() : row["category_id"].get<std::string>(),
            row["account_id"].get<std::string>(),
            row["date"].get<std::string>());
    }
};

}  // namespace Repositories::Money
