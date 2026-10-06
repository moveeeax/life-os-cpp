/**
 * @file Reports.hpp
 * @brief The numbers of a period, per currency and never across them:
 *        income, expense, net, the fixed share, by category against the
 *        budget, the average day and the projection to the period's end,
 *        the previous period and the median of the three before it,
 *        recurring charges and merchants seen for the first time.
 */

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "money/Period.hpp"

namespace Money::Reports {

/// One posted row of the ledger, as the repository hands it to the report.
struct Row {
    std::string date;
    std::string currency;
    std::string type;  // income | expense | fx_adjustment
    std::string category_id;
    std::string category_kind;
    std::string flexibility;  // fixed | variable
    std::string merchant_key;
    double amount = 0;  // signed only for fx_adjustment
};

struct Budget {
    double max = 0;
    std::string currency;
};

struct CategoryTotal {
    std::string category_id;
    double spent = 0;
    std::optional<double> budget;        // when the category's budget is in this currency
    std::optional<double> budget_share;  // spent / budget
};

struct CurrencyBlock {
    std::string currency;
    double income = 0;
    double expense = 0;  // expenses plus adjustments (signed)
    double net = 0;
    double fixed_expense = 0;
    double fixed_share = 0;
    double avg_daily = 0;
    double projection = 0;
    std::optional<double> prev_expense;
    std::optional<double> median3_expense;
    std::vector<CategoryTotal> categories;  // expenses by category, largest first
};

struct Recurring {
    std::string merchant_key;
    std::string currency;
    double amount = 0;  // the last charge
    int times = 0;
    std::string last_date;
    std::string next_expected;
};

struct Report {
    std::vector<CurrencyBlock> blocks;
    std::vector<Recurring> recurring;
    std::vector<std::string> new_merchants;
};

namespace detail {

struct Sums {
    double income = 0, expense = 0, fixed = 0;
    std::map<std::string, double> by_category;
};

inline std::map<std::string, Sums> sums_of(const std::vector<Row>& rows) {
    std::map<std::string, Sums> out;
    for (const auto& r : rows) {
        Sums& s = out[r.currency];
        if (r.type == "income") {
            s.income += r.amount;
        } else {
            // An expense, or an adjustment: plus when the bank took more.
            s.expense += r.amount;
            if (r.type == "expense") {
                s.by_category[r.category_id] += r.amount;
                if (r.flexibility == "fixed") {
                    s.fixed += r.amount;
                }
            }
        }
    }
    return out;
}

inline double median_of_three(double a, double b, double c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

inline int day_gap(const std::string& from, const std::string& to) {
    return static_cast<int>((Period::detail::day_of(to) - Period::detail::day_of(from)).count());
}

}  // namespace detail

/**
 * @param period          The report's range.
 * @param period_rows     Posted rows of the period.
 * @param previous_three  Rows of the three periods before it, nearest first (any may be empty).
 * @param budgets         Category id -> budget, for the budget columns.
 * @param merchants_before Merchant keys seen before the period.
 * @param today           For the projection: days elapsed and left.
 */
inline Report build(const Period::Range& period,
                    const std::vector<Row>& period_rows,
                    const std::vector<std::vector<Row>>& previous_three,
                    const std::map<std::string, Budget>& budgets,
                    const std::set<std::string>& merchants_before,
                    std::string_view today) {
    using namespace detail;
    Report out;
    const auto now = sums_of(period_rows);
    std::vector<std::map<std::string, Sums>> past;
    for (const auto& rows : previous_three) {
        past.push_back(sums_of(rows));
    }

    std::set<std::string> currencies;
    for (const auto& [c, _] : now) {
        currencies.insert(c);
    }
    for (const auto& p : past) {
        for (const auto& [c, _] : p) {
            currencies.insert(c);
        }
    }

    // Today counts as elapsed (its rows are in); the projection covers the
    // days after it. A period that is over projects nothing.
    const int total_days = Period::days(period);
    const int including_today = Period::days_left(period, today);
    const int left = including_today > 0 ? including_today - 1 : 0;
    const int elapsed = std::max(1, total_days - left);

    for (const auto& currency : currencies) {
        CurrencyBlock b;
        b.currency = currency;
        const Sums s = now.count(currency) ? now.at(currency) : Sums{};
        b.income = s.income;
        b.expense = s.expense;
        b.net = s.income - s.expense;
        b.fixed_expense = s.fixed;
        b.fixed_share = s.expense > 0 ? s.fixed / s.expense : 0;
        b.avg_daily = s.expense / elapsed;
        b.projection = s.expense + b.avg_daily * left;
        if (!past.empty()) {
            b.prev_expense = past[0].count(currency) ? past[0].at(currency).expense : 0;
        }
        if (past.size() >= 3) {
            const auto e = [&](std::size_t i) { return past[i].count(currency) ? past[i].at(currency).expense : 0; };
            b.median3_expense = median_of_three(e(0), e(1), e(2));
        }
        for (const auto& [category, spent] : s.by_category) {
            CategoryTotal t;
            t.category_id = category;
            t.spent = spent;
            const auto it = budgets.find(category);
            if (it != budgets.end() && it->second.currency == currency && it->second.max > 0) {
                t.budget = it->second.max;
                t.budget_share = spent / it->second.max;
            }
            b.categories.push_back(t);
        }
        std::sort(
            b.categories.begin(), b.categories.end(), [](const auto& x, const auto& y) { return x.spent > y.spent; });
        out.blocks.push_back(b);
    }

    // Recurring: a merchant charged in one currency at least twice, with
    // every gap between 28 and 32 days, over the period and the three before.
    std::map<std::pair<std::string, std::string>, std::vector<std::pair<std::string, double>>> charges;
    const auto collect = [&](const std::vector<Row>& rows) {
        for (const auto& r : rows) {
            if (r.type == "expense" && !r.merchant_key.empty()) {
                charges[{r.merchant_key, r.currency}].push_back({r.date, r.amount});
            }
        }
    };
    collect(period_rows);
    for (const auto& rows : previous_three) {
        collect(rows);
    }
    for (auto& [key, list] : charges) {
        if (list.size() < 2) {
            continue;
        }
        std::sort(list.begin(), list.end());
        bool monthly = true;
        for (std::size_t i = 1; i < list.size() && monthly; ++i) {
            const int gap = day_gap(list[i - 1].first, list[i].first);
            monthly = gap >= 28 && gap <= 32;
        }
        if (!monthly) {
            continue;
        }
        Recurring rec;
        rec.merchant_key = key.first;
        rec.currency = key.second;
        rec.amount = list.back().second;
        rec.times = static_cast<int>(list.size());
        rec.last_date = list.back().first;
        rec.next_expected = Period::detail::text_of(Period::detail::day_of(rec.last_date) + std::chrono::days{30});
        out.recurring.push_back(rec);
    }

    std::set<std::string> seen;
    for (const auto& r : period_rows) {
        if (!r.merchant_key.empty() && !merchants_before.count(r.merchant_key) && seen.insert(r.merchant_key).second) {
            out.new_merchants.push_back(r.merchant_key);
        }
    }
    return out;
}

}  // namespace Money::Reports
