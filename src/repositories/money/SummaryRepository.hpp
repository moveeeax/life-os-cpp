/**
 * @file SummaryRepository.hpp
 * @brief The inputs of the all-accounts summary: the balance of each currency
 *        on every day of a range (the account balance expression bounded by
 *        date, archived accounts left out) and the quotes of a few currencies
 *        over that range, with the last quote before it.
 */

#pragma once

#include <map>
#include <set>
#include <string>

#include "database/Database.hpp"
#include "money/Summary.hpp"

namespace Repositories::Money {

class SummaryRepository {
public:
    /// date → currency → balance, every day from @p from to @p to.
    std::map<std::string, std::map<std::string, double>> daily_balances(const std::string& owner,
                                                                        const std::string& from,
                                                                        const std::string& to) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "WITH acc AS (SELECT id, currency, opening_balance FROM money_accounts "
                "             WHERE owner_id = $1::uuid AND NOT archived), "
                "moves AS ("
                "  SELECT t.account_id AS acc, t.date AS d, "
                "         CASE t.type WHEN 'income' THEN t.amount ELSE -t.amount END AS v "
                "    FROM money_transactions t WHERE t.owner_id = $1::uuid AND t.status = 'posted' "
                "  UNION ALL SELECT x.to_account_id, x.date, COALESCE(x.amount_received, x.amount_sent) "
                "    FROM money_transfers x WHERE x.owner_id = $1::uuid "
                "  UNION ALL SELECT x.from_account_id, x.date, -(x.amount_sent + COALESCE(x.fee, 0)) "
                "    FROM money_transfers x WHERE x.owner_id = $1::uuid), "
                "per AS (SELECT a.currency, m.d, SUM(m.v) AS v FROM moves m JOIN acc a ON a.id = m.acc GROUP BY 1, 2), "
                "base AS (SELECT currency, SUM(opening_balance) AS ob FROM acc GROUP BY 1), "
                "days AS (SELECT generate_series($2::date, $3::date, interval '1 day')::date AS d) "
                "SELECT to_char(days.d, 'YYYY-MM-DD'), base.currency, "
                "       (base.ob + COALESCE((SELECT SUM(per.v) FROM per "
                "                            WHERE per.currency = base.currency AND per.d <= days.d), 0))::float8 "
                "FROM days CROSS JOIN base ORDER BY 1, 2",
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

}  // namespace Repositories::Money
