/**
 * @file ReportBuilder.hpp
 * @brief The numbers of a period as the Reports page and the advisor see
 *        them: per currency, with the previous period, the median of three,
 *        budgets, recurring charges, new merchants, and an optional "as if"
 *        block converted at stored daily rates. One code path for both, so
 *        the advisor's facts are exactly what the page shows.
 */

#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "money/Period.hpp"
#include "money/Rates.hpp"
#include "money/Reports.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/FxRateRepository.hpp"
#include "repositories/money/TransactionRepository.hpp"

namespace Repositories::Money::ReportBuilder {

using json = nlohmann::json;

inline constexpr int kRecurringDays = 120;

namespace detail {

inline ::Money::Reports::Row report_row(const json& r) {
    ::Money::Reports::Row row;
    row.date = r.value("date", "");
    row.currency = r.value("currency", "");
    row.type = r.value("type", "");
    row.category_id = r["category_id"].is_string() ? r["category_id"].get<std::string>() : "";
    row.category_kind = r.value("category_kind", "");
    row.flexibility = r.value("flexibility", "variable");
    row.merchant_key = r.value("merchant_key", "");
    row.amount = r.value("amount", 0.0);
    return row;
}

inline std::vector<::Money::Reports::Row> rows_of(TransactionRepository& repo,
                                                  const std::string& owner,
                                                  const ::Money::Period::Range& range) {
    std::vector<::Money::Reports::Row> out;
    for (const auto& r : repo.report_rows(owner, range.from, range.to)) {
        out.push_back(report_row(r));
    }
    return out;
}

inline json block_json(const ::Money::Reports::CurrencyBlock& b) {
    json categories = json::array();
    for (const auto& c : b.categories) {
        categories.push_back({{"category_id", c.category_id},
                              {"spent", c.spent},
                              {"budget", c.budget ? json(*c.budget) : json()},
                              {"budget_share", c.budget_share ? json(*c.budget_share) : json()}});
    }
    return {{"currency", b.currency},
            {"income", b.income},
            {"expense", b.expense},
            {"net", b.net},
            {"fixed_expense", b.fixed_expense},
            {"fixed_share", b.fixed_share},
            {"avg_daily", b.avg_daily},
            {"projection", b.projection},
            {"prev_expense", b.prev_expense ? json(*b.prev_expense) : json()},
            {"median3_expense", b.median3_expense ? json(*b.median3_expense) : json()},
            {"categories", categories}};
}

/// The blocks "as if" in one currency, with the rates used and what could not be converted.
inline json as_if_json(const ::Money::Reports::Report& report, const std::string& target, const std::string& on_date) {
    FxRateRepository rates;
    const auto to = rates.nearest(on_date, target);
    json blocks = json::array();
    json used = json::object();
    int unconverted = 0;
    double income = 0, expense = 0;
    for (const auto& b : report.blocks) {
        const auto from = rates.nearest(on_date, b.currency);
        if (!to || !from) {
            ++unconverted;
            continue;
        }
        const ::Money::Rates::Rate f{from->date, from->quote, from->per_usd};
        const ::Money::Rates::Rate t{to->date, to->quote, to->per_usd};
        const auto inc = ::Money::Rates::convert(b.income, &f, &t);
        const auto exp = ::Money::Rates::convert(b.expense, &f, &t);
        if (!inc || !exp) {
            ++unconverted;
            continue;
        }
        income += inc->amount;
        expense += exp->amount;
        json categories = json::array();
        for (const auto& c : b.categories) {
            categories.push_back(
                {{"category_id", c.category_id}, {"spent", ::Money::Rates::convert(c.spent, &f, &t)->amount}});
        }
        blocks.push_back({{"currency", b.currency},
                          {"income", inc->amount},
                          {"expense", exp->amount},
                          {"net", inc->amount - exp->amount},
                          {"rate_date", inc->rate_date},
                          {"categories", categories}});
        used[b.currency] = {{"per_usd", from->per_usd}, {"date", from->date}};
    }
    if (to) {
        used[target] = {{"per_usd", to->per_usd}, {"date", to->date}};
    }
    return {{"currency", target},
            {"blocks", blocks},
            {"income", income},
            {"expense", expense},
            {"net", income - expense},
            {"partial", unconverted > 0 || !to},
            {"unconverted", unconverted},
            {"rates", used},
            {"source", "fawazahmed0/currency-api (CC0)"}};
}

}  // namespace detail

/**
 * @param target  The "as if" currency, or "" for none.
 * @param today   For the projection and the rate day of a running period.
 */
inline json build(const std::string& owner,
                  ::Money::Period::Kind kind,
                  const std::string& kind_text,
                  const ::Money::Period::Range& range,
                  const std::string& today,
                  const std::string& target) {
    using namespace detail;
    TransactionRepository repo;
    const auto rows = rows_of(repo, owner, range);
    std::vector<std::vector<::Money::Reports::Row>> previous;
    ::Money::Period::Range cursor = range;
    for (int i = 0; i < 3; ++i) {
        cursor = ::Money::Period::previous(kind, cursor);
        previous.push_back(rows_of(repo, owner, cursor));
    }
    std::map<std::string, ::Money::Reports::Budget> budgets;
    for (const auto& c : CategoryRepository().list(owner, true)) {
        if (c["budget_max"].is_number() && c["budget_currency"].is_string()) {
            budgets[c["id"].get<std::string>()] = {c["budget_max"].get<double>(), c["budget_currency"]};
        }
    }
    const auto before = repo.merchants_before(owner, range.from);
    const std::set<std::string> seen(before.begin(), before.end());
    // Monthly charges are looked for over at least the 120 days up to the
    // period's end, whatever the period: four charges of a subscription.
    using namespace ::Money::Period::detail;
    const std::string window_from =
        std::min(range.from, text_of(day_of(range.to) - std::chrono::days{kRecurringDays - 1}));
    const auto window = rows_of(repo, owner, {window_from, range.to});
    const auto report = ::Money::Reports::build(range, rows, previous, budgets, seen, today, &window);

    json blocks = json::array();
    for (const auto& b : report.blocks) {
        blocks.push_back(block_json(b));
    }
    json recurring = json::array();
    for (const auto& r : report.recurring) {
        recurring.push_back({{"merchant_key", r.merchant_key},
                             {"currency", r.currency},
                             {"amount", r.amount},
                             {"times", r.times},
                             {"last_date", r.last_date},
                             {"next_expected", r.next_expected}});
    }
    json out{{"period", {{"kind", kind_text}, {"from", range.from}, {"to", range.to}}},
             {"blocks", blocks},
             {"recurring", recurring},
             {"new_merchants", report.new_merchants}};
    if (!target.empty()) {
        out["as_if"] = as_if_json(report, target, std::min(range.to, today));
    }
    return out;
}

}  // namespace Repositories::Money::ReportBuilder
