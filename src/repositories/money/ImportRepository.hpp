/**
 * @file ImportRepository.hpp
 * @brief The one-off copy of a user's money from another tool (spec §9): the
 *        JSON of scripts/notion-money-export.py loaded in one database
 *        transaction. Every row carries the id it had in the source as
 *        `external_id`; a second run updates those rows instead of adding
 *        them again. References between rows (a transaction's account, an
 *        adjustment's original) are by external id. A row that breaks a rule
 *        of the model is rolled back alone and listed with the reason; the
 *        rest goes in.
 */

#pragma once

#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/Errors.hpp"
#include "repositories/money/TransactionRepository.hpp"
#include "repositories/money/TransferRepository.hpp"

namespace Repositories::Money {

class ImportRepository {
public:
    /// The sections the import reads, in the order they go in.
    static constexpr const char* kSections[] = {"currencies", "accounts", "categories", "transactions", "transfers"};

    /// Load @p doc for @p owner. Answers created/updated counts per section and
    /// the rejected rows: {section, external_id, reason}.
    nlohmann::json run(const std::string& owner, const nlohmann::json& doc) {
        return Database::get().execute_write([&](auto& txn) {
            Run<std::remove_reference_t<decltype(txn)>> r(txn, owner);
            r.currencies(section(doc, "currencies"));
            r.accounts(section(doc, "accounts"));
            r.categories(section(doc, "categories"));
            r.transactions(section(doc, "transactions"));
            r.transfers(section(doc, "transfers"));
            nlohmann::json out = r.counts;
            out["rejected"] = r.rejected;
            return out;
        });
    }

private:
    static nlohmann::json section(const nlohmann::json& doc, const char* name) {
        if (!doc.contains(name) || doc[name].is_null()) {
            return nlohmann::json::array();
        }
        return doc[name];
    }

    // ── field readers: a wrong type is the row's fault, not the request's ──

    static std::string text(const nlohmann::json& row, const char* key, const std::string& fallback = "") {
        if (!row.contains(key) || row[key].is_null()) {
            return fallback;
        }
        if (!row[key].is_string()) {
            throw Invariant(std::string(key) + " must be text");
        }
        return row[key].get<std::string>();
    }

    static std::string required_text(const nlohmann::json& row, const char* key) {
        std::string v = text(row, key);
        if (v.empty()) {
            throw Invariant(std::string(key) + " is required");
        }
        return v;
    }

    static std::optional<std::string> opt_text(const nlohmann::json& row, const char* key) {
        std::string v = text(row, key);
        if (v.empty()) {
            return std::nullopt;
        }
        return v;
    }

    static std::optional<double> opt_number(const nlohmann::json& row, const char* key) {
        if (!row.contains(key) || row[key].is_null()) {
            return std::nullopt;
        }
        if (!row[key].is_number()) {
            throw Invariant(std::string(key) + " must be a number");
        }
        return row[key].get<double>();
    }

    static double number(const nlohmann::json& row, const char* key) {
        const auto v = opt_number(row, key);
        if (!v.has_value()) {
            throw Invariant(std::string(key) + " is required");
        }
        return *v;
    }

    template <typename Txn>
    struct Run {
        Txn& txn;
        const std::string& owner;
        nlohmann::json counts = nlohmann::json::object();
        nlohmann::json rejected = nlohmann::json::array();
        std::map<std::string, std::string> account_ids, category_ids, transaction_ids;

        Run(Txn& t, const std::string& o) : txn(t), owner(o) {}

        /// One row in its own savepoint: a failure undoes that row only.
        template <typename F>
        void row(const char* name, const nlohmann::json& r, F&& work) {
            std::string ext;
            for (const char* key : {"external_id", "code"}) {
                if (ext.empty() && r.is_object() && r.contains(key) && r[key].is_string()) {
                    ext = r[key].get<std::string>();
                }
            }
            txn.exec("SAVEPOINT money_import_row");
            try {
                if (!r.is_object()) {
                    throw Invariant("a row must be an object");
                }
                const bool created = detail::translate_sql([&] { return work(r); }, &detail::translate);
                txn.exec("RELEASE SAVEPOINT money_import_row");
                const char* key = created ? "created" : "updated";
                counts[name][key] = counts[name][key].template get<int>() + 1;
            } catch (const std::exception& e) {
                txn.exec("ROLLBACK TO SAVEPOINT money_import_row");
                rejected.push_back({{"section", name}, {"external_id", ext}, {"reason", e.what()}});
            }
        }

        template <typename F>
        void each(const char* name, const nlohmann::json& rows, F&& work) {
            counts[name] = {{"created", 0}, {"updated", 0}};
            for (const auto& r : rows) {
                row(name, r, work);
            }
        }

        /// The id of an imported row: one of this run, or one an earlier run left.
        std::string resolve(std::map<std::string, std::string>& seen,
                            const char* table,
                            const std::string& ext,
                            const char* what) {
            if (const auto it = seen.find(ext); it != seen.end()) {
                return it->second;
            }
            auto q = txn.exec_params(
                std::string("SELECT id::text FROM ") + table + " WHERE owner_id = $1::uuid AND external_id = $2",
                owner,
                ext);
            if (q.empty()) {
                throw Invariant(std::string("no imported ") + what + " " + ext);
            }
            seen[ext] = q[0][0].template as<std::string>();
            return seen[ext];
        }

        std::optional<std::string> existing(const char* table, const std::string& ext) {
            auto q = txn.exec_params(
                std::string("SELECT id::text FROM ") + table + " WHERE owner_id = $1::uuid AND external_id = $2",
                owner,
                ext);
            if (q.empty()) {
                return std::nullopt;
            }
            return q[0][0].template as<std::string>();
        }

        void currencies(const nlohmann::json& rows) {
            each("currencies", rows, [&](const nlohmann::json& r) {
                const std::string code = required_text(r, "code");
                const auto decimals = opt_number(r, "decimals");
                auto q = txn.exec_params(
                    "INSERT INTO money_currencies (owner_id, code, name, role, decimals) "
                    "VALUES ($1::uuid, $2, $3, $4, $5) ON CONFLICT (owner_id, code) DO UPDATE SET "
                    " name = EXCLUDED.name, role = EXCLUDED.role, decimals = EXCLUDED.decimals "
                    "RETURNING (xmax = 0)",
                    owner,
                    code,
                    text(r, "name"),
                    opt_text(r, "role"),
                    decimals.has_value() ? static_cast<int>(*decimals) : 2);
                return q[0][0].template as<bool>();
            });
        }

        void accounts(const nlohmann::json& rows) {
            each("accounts", rows, [&](const nlohmann::json& r) {
                const std::string ext = required_text(r, "external_id");
                const std::string currency = required_text(r, "currency");
                const auto id = existing("money_accounts", ext);
                if (id.has_value()) {
                    if (AccountRepository::currency_in(txn, owner, *id) != currency) {
                        throw Invariant("the currency of an imported account does not change");
                    }
                    txn.exec_params(
                        "UPDATE money_accounts SET name = $3, bank = $4, kind = $5, last4 = $6, "
                        " opening_balance = $7, opening_date = $8::date, updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        *id,
                        required_text(r, "name"),
                        text(r, "bank"),
                        text(r, "kind", "card"),
                        text(r, "last4"),
                        opt_number(r, "opening_balance").value_or(0),
                        opt_text(r, "opening_date"));
                    account_ids[ext] = *id;
                    return false;
                }
                auto q = txn.exec_params(
                    "INSERT INTO money_accounts (owner_id, name, bank, kind, currency, last4, opening_balance, "
                    " opening_date, external_id) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8::date, $9) "
                    "RETURNING id::text",
                    owner,
                    required_text(r, "name"),
                    text(r, "bank"),
                    text(r, "kind", "card"),
                    currency,
                    text(r, "last4"),
                    opt_number(r, "opening_balance").value_or(0),
                    opt_text(r, "opening_date"),
                    ext);
                account_ids[ext] = q[0][0].template as<std::string>();
                return true;
            });
        }

        void categories(const nlohmann::json& rows) {
            each("categories", rows, [&](const nlohmann::json& r) {
                const std::string ext = required_text(r, "external_id");
                const std::string kind = text(r, "kind", "expense");
                const auto id = existing("money_categories", ext);
                if (id.has_value()) {
                    if (CategoryRepository::kind_in(txn, owner, *id) != kind) {
                        auto used = txn.exec_params(
                            "SELECT 1 FROM money_transactions WHERE owner_id = $1::uuid AND category_id = $2::uuid "
                            "LIMIT 1",
                            owner,
                            *id);
                        if (!used.empty()) {
                            throw Invariant("the category has rows: its kind does not change");
                        }
                    }
                    txn.exec_params(
                        "UPDATE money_categories SET name = $3, kind = $4, flexibility = $5, budget_max = $6, "
                        " budget_currency = $7, updated_at = now() WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        *id,
                        required_text(r, "name"),
                        kind,
                        text(r, "flexibility", "variable"),
                        opt_number(r, "budget_max"),
                        opt_text(r, "budget_currency"));
                    category_ids[ext] = *id;
                    return false;
                }
                auto q = txn.exec_params(
                    "INSERT INTO money_categories (owner_id, name, kind, flexibility, budget_max, budget_currency, "
                    " external_id) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7) RETURNING id::text",
                    owner,
                    required_text(r, "name"),
                    kind,
                    text(r, "flexibility", "variable"),
                    opt_number(r, "budget_max"),
                    opt_text(r, "budget_currency"),
                    ext);
                category_ids[ext] = q[0][0].template as<std::string>();
                return true;
            });
        }

        void transactions(const nlohmann::json& rows) {
            // Originals first: an adjustment points at a row that must be in.
            nlohmann::json ordered = nlohmann::json::array();
            for (const auto& r : rows) {
                if (!(r.is_object() && r.value("type", std::string()) == "fx_adjustment")) {
                    ordered.push_back(r);
                }
            }
            for (const auto& r : rows) {
                if (r.is_object() && r.value("type", std::string()) == "fx_adjustment") {
                    ordered.push_back(r);
                }
            }
            each("transactions", ordered, [&](const nlohmann::json& r) { return transaction(r); });
        }

        bool transaction(const nlohmann::json& r) {
            const std::string ext = required_text(r, "external_id");
            TransactionRepository::Input in;
            in.type = required_text(r, "type");
            in.date = required_text(r, "date");
            in.time = opt_text(r, "time");
            in.account_id = resolve(account_ids, "money_accounts", required_text(r, "account"), "account");
            in.amount = number(r, "amount");
            if (const auto cat = opt_text(r, "category"); cat.has_value()) {
                in.category_id = resolve(category_ids, "money_categories", *cat, "category");
            }
            in.merchant = text(r, "merchant");
            in.name = required_text(r, "name");
            in.receipt_amount = opt_number(r, "receipt_amount");
            in.receipt_currency = opt_text(r, "receipt_currency");
            in.fx_note = text(r, "fx_note");
            if (const auto adj = opt_text(r, "adjusts"); adj.has_value()) {
                in.adjusts_id = resolve(transaction_ids, "money_transactions", *adj, "transaction");
            }
            in.note = text(r, "note");
            in.source = "import";
            in.status = "posted";
            in.external_id = ext;

            const auto id = existing("money_transactions", ext);
            if (!id.has_value()) {
                transaction_ids[ext] = TransactionRepository::insert_in(txn, owner, in);
                return true;
            }
            auto cur = txn.exec_params(
                "SELECT type, account_id::text, COALESCE(adjusts_id::text, '') FROM money_transactions "
                "WHERE owner_id = $1::uuid AND id = $2::uuid",
                owner,
                *id);
            if (cur[0][0].template as<std::string>() != in.type ||
                cur[0][1].template as<std::string>() != in.account_id ||
                cur[0][2].template as<std::string>() != in.adjusts_id.value_or("")) {
                throw Invariant("the type, account and original of an imported row do not change");
            }
            if (in.type != "fx_adjustment") {
                if (!in.category_id.has_value()) {
                    throw Invariant("an " + in.type + " needs a category");
                }
                if (CategoryRepository::kind_in(txn, owner, *in.category_id) != in.type) {
                    throw Invariant("an " + in.type + " needs a category of kind " + in.type);
                }
            }
            txn.exec_params(
                "UPDATE money_transactions SET date = $3::date, time = $4::time, amount = $5, category_id = $6::uuid, "
                " merchant = $7, merchant_key = $8, name = $9, receipt_amount = $10, receipt_currency = $11, "
                " fx_note = $12, note = $13, updated_at = now() WHERE owner_id = $1::uuid AND id = $2::uuid",
                owner,
                *id,
                in.date,
                in.time,
                in.amount,
                in.category_id,
                in.merchant,
                ::Money::merchant_key(in.merchant),
                in.name,
                in.receipt_amount,
                in.receipt_currency,
                in.fx_note,
                in.note);
            transaction_ids[ext] = *id;
            return false;
        }

        void transfers(const nlohmann::json& rows) {
            each("transfers", rows, [&](const nlohmann::json& r) {
                const std::string ext = required_text(r, "external_id");
                const std::string from =
                    resolve(account_ids, "money_accounts", required_text(r, "from_account"), "account");
                const std::string to =
                    resolve(account_ids, "money_accounts", required_text(r, "to_account"), "account");
                const auto received = opt_number(r, "amount_received");
                TransferRepository::check_pair(txn, owner, from, to, received);
                const auto id = existing("money_transfers", ext);
                if (id.has_value()) {
                    auto cur = txn.exec_params(
                        "SELECT from_account_id::text, to_account_id::text FROM money_transfers "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        *id);
                    if (cur[0][0].template as<std::string>() != from || cur[0][1].template as<std::string>() != to) {
                        throw Invariant("the accounts of an imported transfer do not change");
                    }
                    txn.exec_params(
                        "UPDATE money_transfers SET date = $3::date, amount_sent = $4, amount_received = $5, fee = $6, "
                        " name = $7, note = $8, updated_at = now() WHERE owner_id = $1::uuid AND id = $2::uuid",
                        owner,
                        *id,
                        required_text(r, "date"),
                        number(r, "amount_sent"),
                        received,
                        opt_number(r, "fee"),
                        text(r, "name"),
                        text(r, "note"));
                    return false;
                }
                txn.exec_params(
                    "INSERT INTO money_transfers (owner_id, date, from_account_id, to_account_id, amount_sent, "
                    " amount_received, fee, name, note, external_id) "
                    "VALUES ($1::uuid, $2::date, $3::uuid, $4::uuid, $5, $6, $7, $8, $9, $10)",
                    owner,
                    required_text(r, "date"),
                    from,
                    to,
                    number(r, "amount_sent"),
                    received,
                    opt_number(r, "fee"),
                    text(r, "name"),
                    text(r, "note"),
                    ext);
                return true;
            });
        }
    };
};

}  // namespace Repositories::Money
