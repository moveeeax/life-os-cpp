/**
 * @file FxRateRepository.hpp
 * @brief Daily quotes against the US dollar (money_fx_rates), shared by every
 *        user. A day's rates are written once by the money_rates job; a read
 *        for a day without rates takes the nearest earlier day and says so.
 */

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>

#include "database/Database.hpp"

namespace Repositories::Money {

class FxRateRepository {
public:
    struct Rate {
        std::string date;  // the day the rate is from (may be earlier than asked)
        std::string quote;
        double per_usd = 0;
    };

    /// Writes the day's quotes; a quote already stored for the day is replaced.
    void put_day(const std::string& date, const std::map<std::string, double>& per_usd) {
        Database::get().execute_write([&](auto& txn) {
            for (const auto& [quote, rate] : per_usd) {
                txn.exec_params(
                    "INSERT INTO money_fx_rates (date, quote, per_usd) VALUES ($1::date, $2, $3) "
                    "ON CONFLICT (date, quote) DO UPDATE SET per_usd = EXCLUDED.per_usd, fetched_at = now()",
                    date,
                    quote,
                    rate);
            }
            return true;
        });
    }

    /// The quote's rate on `date` or the nearest earlier day; USD is always 1 of today.
    std::optional<Rate> nearest(const std::string& date, const std::string& quote) {
        if (quote == "USD") {
            return Rate{date, "USD", 1.0};
        }
        return Database::get().execute_read([&](auto& txn) -> std::optional<Rate> {
            auto r = txn.exec_params(
                "SELECT to_char(date, 'YYYY-MM-DD'), per_usd FROM money_fx_rates WHERE quote = $2 AND date <= $1::date "
                "ORDER BY date DESC LIMIT 1",
                date,
                quote);
            if (r.empty()) {
                return std::nullopt;
            }
            return Rate{r[0][0].template as<std::string>(), quote, r[0][1].template as<double>()};
        });
    }

    bool has_day(const std::string& date) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params("SELECT 1 FROM money_fx_rates WHERE date = $1::date LIMIT 1", date);
            return !r.empty();
        });
    }

    /// Every currency any user keeps, plus the three the reports always offer.
    std::set<std::string> quotes_needed() {
        return Database::get().execute_read([&](auto& txn) {
            std::set<std::string> out{"USD", "EUR", "KZT"};
            auto r = txn.exec("SELECT DISTINCT code FROM money_currencies WHERE NOT archived");
            for (const auto& row : r) {
                out.insert(row[0].template as<std::string>());
            }
            return out;
        });
    }
};

}  // namespace Repositories::Money
