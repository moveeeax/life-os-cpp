/**
 * @file SummaryRepository.hpp
 * @brief The inputs of the all-accounts summary: the balance of each currency
 *        on every day of a range (the account balance expression bounded by
 *        date, archived accounts left out) and the quotes of a few currencies
 *        over that range, with the last quote before it.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "money/Period.hpp"
#include "money/Summary.hpp"
#include "repositories/money/AccountRepository.hpp"

namespace Repositories::Money {

class SummaryRepository {
    // The non-archived accounts and every posted move of their money, one row
    // per move: the account balance expression of AccountRepository.
    static constexpr const char* kMoves =
        "WITH acc AS (SELECT id, currency, opening_balance FROM money_accounts "
        "             WHERE owner_id = $1::uuid AND NOT archived), "
        "moves AS ("
        "  SELECT t.account_id AS acc, t.date AS d, "
        "         CASE t.type WHEN 'income' THEN t.amount ELSE -t.amount END AS v "
        "    FROM money_transactions t WHERE t.owner_id = $1::uuid AND t.status = 'posted' "
        "  UNION ALL SELECT x.to_account_id, x.date, COALESCE(x.amount_received, x.amount_sent) "
        "    FROM money_transfers x WHERE x.owner_id = $1::uuid "
        "  UNION ALL SELECT x.from_account_id, x.date, -(x.amount_sent + COALESCE(x.fee, 0)) "
        "    FROM money_transfers x WHERE x.owner_id = $1::uuid)";

public:
    /// date → currency → balance, every day from @p from to @p to: the daily sums
    /// once, then a running total per currency.
    std::map<std::string, std::map<std::string, double>> daily_balances(const std::string& owner,
                                                                        const std::string& from,
                                                                        const std::string& to) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                std::string(kMoves) +
                    ", per AS MATERIALIZED (SELECT a.currency, m.d, SUM(m.v) AS v FROM moves m "
                    "                      JOIN acc a ON a.id = m.acc GROUP BY 1, 2), "
                    "base AS (SELECT currency, SUM(opening_balance) AS ob FROM acc GROUP BY 1), "
                    "before AS (SELECT currency, SUM(v) AS v FROM per WHERE d < $2::date GROUP BY 1), "
                    "days AS (SELECT generate_series($2::date, $3::date, interval '1 day')::date AS d), "
                    "grid AS (SELECT days.d, base.currency, base.ob + COALESCE(before.v, 0) AS start "
                    "         FROM days CROSS JOIN base LEFT JOIN before ON before.currency = base.currency) "
                    "SELECT to_char(g.d, 'YYYY-MM-DD'), g.currency, "
                    "       (g.start + SUM(COALESCE(p.v, 0)) OVER (PARTITION BY g.currency ORDER BY g.d))::float8 "
                    "FROM grid g LEFT JOIN per p ON p.currency = g.currency AND p.d = g.d ORDER BY 1, 2",
                owner,
                from,
                to);
            std::map<std::string, std::map<std::string, double>> out;
            for (const auto& row : r) {
                out[row[0].template as<std::string>()][row[1].template as<std::string>()] =
                    row[2].template as<double>();
            }
            return out;
        });
    }

    /// account id → balance at the end of @p date, non-archived accounts.
    std::map<std::string, double> balances_at(const std::string& owner, const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            auto r =
                txn.exec_params(std::string(kMoves) +
                                    " SELECT a.id::text, (a.opening_balance + COALESCE(SUM(m.v) "
                                    "        FILTER (WHERE m.d <= $2::date), 0))::float8 "
                                    "FROM acc a LEFT JOIN moves m ON m.acc = a.id GROUP BY a.id, a.opening_balance",
                                owner,
                                date);
            std::map<std::string, double> out;
            for (const auto& row : r) {
                out[row[0].template as<std::string>()] = row[1].template as<double>();
            }
            return out;
        });
    }

    /// The quotes of @p currencies from the last one on or before @p from up to @p to, oldest first.
    ::Money::Summary::Quotes quotes(const std::set<std::string>& currencies,
                                    const std::string& from,
                                    const std::string& to) {
        return Database::get().execute_read([&](auto& txn) {
            ::Money::Summary::Quotes out;
            for (const auto& currency : currencies) {
                if (currency == "USD") {
                    continue;
                }
                auto r = txn.exec_params(
                    "SELECT to_char(date, 'YYYY-MM-DD'), per_usd::float8 FROM money_fx_rates "
                    "WHERE quote = $1 AND date <= $3::date AND date >= COALESCE("
                    "  (SELECT max(date) FROM money_fx_rates WHERE quote = $1 AND date <= $2::date), $2::date) "
                    "ORDER BY date",
                    currency,
                    from,
                    to);
                auto& list = out[currency];
                for (const auto& row : r) {
                    list.push_back(
                        ::Money::Summary::Quote{row[0].template as<std::string>(), row[1].template as<double>()});
                }
            }
            return out;
        });
    }
};

/// The summary of all accounts in @p target over the @p days up to @p date (spec
/// docs/superpowers/specs/2026-10-13-money-summary-design.md), or nullopt when
/// @p target has no quote on @p date.
inline std::optional<nlohmann::json> summary_json(const std::string& owner,
                                                  const std::string& target,
                                                  int days,
                                                  const std::string& date) {
    using nlohmann::json;
    namespace S = ::Money::Summary;
    namespace P = ::Money::Period::detail;
    const auto last = P::day_of(date);
    const std::string from = P::text_of(last - std::chrono::days{days - 1});

    SummaryRepository repo;
    const auto balances = repo.daily_balances(owner, from, date);
    const json accounts = AccountRepository().list(owner, false);
    const auto at_date = repo.balances_at(owner, date);
    std::set<std::string> currencies{target};
    for (const auto& [day, by_currency] : balances) {
        for (const auto& entry : by_currency) {
            currencies.insert(entry.first);
        }
    }
    for (const auto& a : accounts) {
        currencies.insert(a["currency"].get<std::string>());
    }
    const auto quotes = repo.quotes(currencies, from, date);
    if (!S::nearest(quotes, target, date)) {
        return std::nullopt;
    }
    const std::map<std::string, double> none;
    const auto on = [&](const std::string& day) -> const std::map<std::string, double>& {
        const auto it = balances.find(day);
        return it == balances.end() ? none : it->second;
    };

    json series = json::array();
    for (auto d = P::day_of(from); d <= last; d += std::chrono::days{1}) {
        const std::string day = P::text_of(d);
        const auto t = S::convert_day(on(day), quotes, target, day);
        series.push_back(json{{"date", day}, {"total", t.total}, {"partial", t.partial}});
    }

    json rows = json::array();
    std::map<std::string, double> unconverted;
    std::string rate_date = date;
    const auto target_quote = S::nearest(quotes, target, date);
    for (const auto& a : accounts) {
        const std::string currency = a["currency"].get<std::string>();
        // The balance at the end of the date, like the total; the list carries the all-time one.
        const auto found = at_date.find(a["id"].get<std::string>());
        const double balance = found == at_date.end() ? 0.0 : found->second;
        const auto quote = S::nearest(quotes, currency, date);
        const auto rate = S::rate_per_target(quotes, currency, target, date);
        if (!rate || !quote) {
            if (balance != 0) {  // an empty account in an unquoted currency changes nothing
                unconverted[currency] += balance;
            }
            continue;
        }
        const std::string day = std::min(quote->date, target_quote->date);
        rate_date = std::min(rate_date, day);
        rows.push_back(json{{"id", a["id"]},
                            {"name", a["name"]},
                            {"kind", a["kind"]},
                            {"currency", currency},
                            {"balance", balance},
                            {"rate", *rate},
                            {"rate_date", day},
                            {"value", balance / *rate}});
    }
    std::sort(rows.begin(), rows.end(), [](const json& x, const json& y) {
        return x["value"].get<double>() > y["value"].get<double>();
    });
    json not_converted = json::array();
    for (const auto& [currency, amount] : unconverted) {
        not_converted.push_back(json{{"currency", currency}, {"amount", amount}});
    }

    const auto today = S::convert_day(on(date), quotes, target, date);
    const auto change = S::split(on(from), on(date), quotes, target, from, date);
    // The split covers the currencies quoted on both days; money without a quote
    // at either end makes the change partial, and it then differs from the line.
    bool change_partial = false;
    for (const auto* day : {&from, &date}) {
        for (const auto& [currency, amount] : on(*day)) {
            if (amount != 0 && (!S::rate_per_target(quotes, currency, target, from) ||
                                !S::rate_per_target(quotes, currency, target, date))) {
                change_partial = true;
            }
        }
    }
    return json{{"currency", target},
                {"date", date},
                {"total", today.total},
                {"partial", today.partial || !not_converted.empty()},
                {"rate_date", rate_date},
                {"accounts", rows},
                {"not_converted", not_converted},
                {"change",
                 {{"from", from},
                  {"total", change.total},
                  {"ledger", change.ledger},
                  {"rates", change.rates},
                  {"partial", change_partial}}},
                {"days", series}};
}

}  // namespace Repositories::Money
