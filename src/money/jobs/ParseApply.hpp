/**
 * @file ParseApply.hpp
 * @brief The lines of a money parse created with apply, posted by the worker
 *        inside the transaction that closes the job (no accept by the client).
 *
 * Which account a line goes on, in order: the account the request named, the
 * account the parse named, the one open account of the line's currency (the
 * receipt currency the parse read, or the currency every open account shares).
 * A line with such an account and a category is posted. Any other line is
 * created pending, so it waits in the inbox instead of being lost: on the
 * account the merchant memory last saw for its merchant, else the account of
 * the last posted row, else the first open account; with no category it waits
 * under a guessed one (detail::waiting_category). Only a user with no open
 * account, or no category of the line's kind, gets nothing: the line is
 * reported in `skipped` and stays in the job.
 */

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "money/Merchant.hpp"
#include "money/ParseAnswer.hpp"
#include "repositories/money/Errors.hpp"
#include "repositories/money/TransactionRepository.hpp"

namespace Jobs::MoneyParse {

namespace detail {

struct OpenAccounts {
    std::vector<std::string> ids;                 // by position, then name
    std::map<std::string, std::string> currency;  // id -> currency
};

template <typename Txn>
OpenAccounts open_accounts_in(Txn& txn, const std::string& owner) {
    OpenAccounts out;
    for (const auto& row :
         txn.exec_params("SELECT id::text, currency FROM money_accounts WHERE owner_id = $1::uuid AND NOT archived "
                         "ORDER BY position, name",
                         owner)) {
        const auto id = row[0].template as<std::string>();
        out.ids.push_back(id);
        out.currency[id] = row[1].template as<std::string>();
    }
    return out;
}

/// The account a line is posted on; nullopt when none follows from the rules above.
inline std::optional<std::string> posting_account(const OpenAccounts& open,
                                                  const ::Money::Parse::Line& line,
                                                  const std::optional<std::string>& requested) {
    if (requested && open.currency.count(*requested)) {
        return requested;
    }
    if (line.account_id && open.currency.count(*line.account_id)) {
        return line.account_id;
    }
    std::optional<std::string> currency = line.receipt_currency;
    if (!currency) {
        std::set<std::string> all;
        for (const auto& [id, code] : open.currency) {
            all.insert(code);
        }
        if (all.size() == 1) {
            currency = *all.begin();
        }
    }
    if (!currency) {
        return std::nullopt;
    }
    std::optional<std::string> only;
    for (const auto& id : open.ids) {
        if (open.currency.at(id) == *currency) {
            if (only) {
                return std::nullopt;  // two accounts of the currency: the parse cannot choose
            }
            only = id;
        }
    }
    return only;
}

/// Where a pending line waits: the merchant's remembered account, the last posted row's, the first open one.
template <typename Txn>
std::optional<std::string> waiting_account(Txn& txn,
                                           const std::string& owner,
                                           const OpenAccounts& open,
                                           const ::Money::Parse::Line& line) {
    const auto open_one = [&](const pqxx::result& r) -> std::optional<std::string> {
        if (r.empty() || r[0][0].is_null()) {
            return std::nullopt;
        }
        const auto id = r[0][0].template as<std::string>();
        return open.currency.count(id) ? std::optional<std::string>(id) : std::nullopt;
    };
    const std::string key = ::Money::merchant_key(line.merchant);
    if (!key.empty()) {
        if (auto id = open_one(txn.exec_params(
                "SELECT account_id::text FROM money_merchants WHERE owner_id = $1::uuid AND merchant_key = $2",
                owner,
                key))) {
            return id;
        }
    }
    if (auto id = open_one(txn.exec_params(
            "SELECT t.account_id::text FROM money_transactions t JOIN money_accounts a ON a.id = t.account_id "
            "WHERE t.owner_id = $1::uuid AND t.status = 'posted' AND NOT a.archived "
            "ORDER BY t.date DESC, t.created_at DESC LIMIT 1",
            owner))) {
        return id;
    }
    if (!open.ids.empty()) {
        return open.ids.front();
    }
    return std::nullopt;
}

/// The category a pending line without one waits under (every income and
/// expense row has one): the merchant's remembered category, else the one of
/// the owner's last posted row of the type, else the first open category of
/// the kind. The inbox lets the person change it before posting.
template <typename Txn>
std::optional<std::string> waiting_category(Txn& txn, const std::string& owner, const ::Money::Parse::Line& line) {
    const auto first = [](const pqxx::result& r) -> std::optional<std::string> {
        if (r.empty() || r[0][0].is_null()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    };
    const std::string key = ::Money::merchant_key(line.merchant);
    if (!key.empty()) {
        if (auto id = first(txn.exec_params(
                "SELECT m.category_id::text FROM money_merchants m JOIN money_categories c ON c.id = m.category_id "
                "WHERE m.owner_id = $1::uuid AND m.merchant_key = $2 AND c.kind = $3 AND NOT c.archived",
                owner,
                key,
                line.type))) {
            return id;
        }
    }
    if (auto id = first(txn.exec_params(
            "SELECT t.category_id::text FROM money_transactions t JOIN money_categories c ON c.id = t.category_id "
            "WHERE t.owner_id = $1::uuid AND t.status = 'posted' AND t.type = $2 AND c.kind = $2 AND NOT c.archived "
            "ORDER BY t.date DESC, t.created_at DESC LIMIT 1",
            owner,
            line.type))) {
        return id;
    }
    return first(txn.exec_params(
        "SELECT id::text FROM money_categories WHERE owner_id = $1::uuid AND kind = $2 AND NOT archived "
        "ORDER BY position, name LIMIT 1",
        owner,
        line.type));
}

}  // namespace detail

/**
 * Posts @p lines for @p owner inside @p txn. Returns the created rows as
 * `[{id, status}]` (status `posted` or `pending`); a line that cannot become a
 * row at all is appended to @p skipped as `{kind: "money", line, reason}`.
 * @param source the ledger's `source` of the rows ("text").
 */
template <typename Txn>
nlohmann::json apply_in(Txn& txn,
                        const std::string& owner,
                        const std::vector<::Money::Parse::Line>& lines,
                        const std::optional<std::string>& requested_account,
                        const std::string& source,
                        nlohmann::json& skipped) {
    using Repositories::Money::TransactionRepository;
    const auto open = detail::open_accounts_in(txn, owner);
    nlohmann::json out = nlohmann::json::array();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        TransactionRepository::Input in;
        in.type = l.type;
        in.date = l.date;
        in.time = l.time;
        in.amount = l.amount;
        in.category_id = l.category_id;
        in.merchant = l.merchant;
        in.name = l.name;
        in.receipt_amount = l.receipt_amount;
        in.receipt_currency = l.receipt_currency;
        in.fx_note = l.fx_note;
        in.note = l.note;
        in.source = source;
        const auto posting = detail::posting_account(open, l, requested_account);
        // The checks below throw before any statement fails, so the transaction stays usable.
        if (posting && l.category_id) {
            in.account_id = *posting;
            in.status = "posted";
            try {
                const auto row = TransactionRepository::create_in(txn, owner, in);
                out.push_back({{"id", row["id"]}, {"status", "posted"}});
                continue;
            } catch (const Repositories::Money::Invariant&) {
                // Falls through to a pending row: the inbox shows what did not fit.
            }
        }
        const auto waiting = posting ? posting : detail::waiting_account(txn, owner, open, l);
        if (!waiting) {
            skipped.push_back({{"kind", "money"}, {"line", i}, {"reason", "no_account"}});
            continue;
        }
        in.account_id = *waiting;
        in.status = "pending";
        if (!in.category_id) {
            in.category_id = detail::waiting_category(txn, owner, l);
        }
        if (!in.category_id) {
            skipped.push_back({{"kind", "money"}, {"line", i}, {"reason", "no_category"}});
            continue;
        }
        try {
            const auto row = TransactionRepository::create_in(txn, owner, in);
            out.push_back({{"id", row["id"]}, {"status", "pending"}});
        } catch (const Repositories::Money::Invariant& e) {
            skipped.push_back({{"kind", "money"}, {"line", i}, {"reason", e.message()}});
        }
    }
    return out;
}

}  // namespace Jobs::MoneyParse
