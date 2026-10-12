/**
 * @file Summary.hpp
 * @brief All accounts in one currency, explicitly: each currency converted
 *        through its quote against the US dollar on the day (or the nearest
 *        earlier one), a currency without a quote left unconverted and the
 *        total marked partial; the change over a period split into what the
 *        ledger did (at the end rates) and what the rates did (the start
 *        balances re-priced). The two parts add up to the change.
 */

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Money::Summary {

struct Quote {
    std::string date;  // YYYY-MM-DD
    double per_usd = 0;
};

/// Quotes per currency, oldest first.
using Quotes = std::map<std::string, std::vector<Quote>>;

/// The quote of @p currency on @p date or the nearest earlier day; USD is always 1.
inline std::optional<Quote> nearest(const Quotes& q, const std::string& currency, std::string_view date) {
    if (currency == "USD") {
        return Quote{std::string(date), 1.0};
    }
    const auto it = q.find(currency);
    if (it == q.end()) {
        return std::nullopt;
    }
    std::optional<Quote> best;
    for (const auto& quote : it->second) {
        if (quote.date <= date && quote.per_usd > 0) {
            best = quote;
        } else if (quote.date > date) {
            break;
        }
    }
    return best;
}

/// Units of @p currency per 1 unit of @p target on @p date, or nullopt without quotes.
inline std::optional<double> rate_per_target(const Quotes& q,
                                             const std::string& currency,
                                             const std::string& target,
                                             std::string_view date) {
    const auto from = nearest(q, currency, date);
    const auto to = nearest(q, target, date);
    if (!from || !to) {
        return std::nullopt;
    }
    return from->per_usd / to->per_usd;
}

struct DayTotal {
    double total = 0;
    bool partial = false;
};

/// The day's balances per currency in @p target; a currency without a quote is left out (partial).
inline DayTotal convert_day(const std::map<std::string, double>& balances,
                            const Quotes& q,
                            const std::string& target,
                            std::string_view date) {
    DayTotal out;
    for (const auto& [currency, amount] : balances) {
        const auto rate = rate_per_target(q, currency, target, date);
        if (!rate) {
            out.partial = out.partial || amount != 0;
            continue;
        }
        out.total += amount / *rate;
    }
    return out;
}

struct Change {
    double total = 0;
    double ledger = 0;  // the change of the balances, at the end rates
    double rates = 0;   // the start balances re-priced from the start rates to the end rates
};

/// The change between two days in @p target, over the currencies quoted on both days.
inline Change split(const std::map<std::string, double>& start,
                    const std::map<std::string, double>& end,
                    const Quotes& q,
                    const std::string& target,
                    std::string_view from,
                    std::string_view to) {
    std::set<std::string> currencies;
    for (const auto& entry : start) {
        currencies.insert(entry.first);
    }
    for (const auto& entry : end) {
        currencies.insert(entry.first);
    }
    Change out;
    for (const auto& currency : currencies) {
        const auto r0 = rate_per_target(q, currency, target, from);
        const auto r1 = rate_per_target(q, currency, target, to);
        if (!r0 || !r1) {
            continue;
        }
        const auto b0 = start.count(currency) ? start.at(currency) : 0.0;
        const auto b1 = end.count(currency) ? end.at(currency) : 0.0;
        out.ledger += (b1 - b0) / *r1;
        out.rates += b0 / *r1 - b0 / *r0;
    }
    out.total = out.ledger + out.rates;
    return out;
}

}  // namespace Money::Summary
