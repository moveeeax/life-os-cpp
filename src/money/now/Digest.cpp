/**
 * @file Digest.cpp
 * @brief Body for src/money/now/Digest.hpp — compiled once into app_core.
 */

#include "money/now/Digest.hpp"

#include <nlohmann/json.hpp>

#include "cache/Cache.hpp"
#include "money/DigestCache.hpp"
#include "money/Period.hpp"
#include "now/Today.hpp"
#include "repositories/money/ReportBuilder.hpp"
#include "repositories/money/SummaryRepository.hpp"
#include "repositories/money/TransactionRepository.hpp"

namespace Money {

namespace {

nlohmann::json compute(const std::string& owner, const std::string& day) {
    using json = nlohmann::json;
    using Period::Kind;
    Repositories::Money::TransactionRepository tx;
    const json rows = tx.report_rows(owner, day, day);
    const json report =
        Repositories::Money::ReportBuilder::build(owner, Kind::month, "month", Period::of(Kind::month, day), day, "");
    const json currencies = ::Now::Today::money_block(rows, report);
    Repositories::Money::TransactionRepository::Filter f;
    f.from = day;
    f.to = day;
    f.type = "expense";
    f.status = "posted";
    f.limit = static_cast<long>(::Now::Today::kSpendsShown);
    const json spends = ::Now::Today::spends_block(tx.list(owner, f).rows);
    // All accounts in USD with the change over 30 days, for the corners and outside dashboards.
    json net_worth;
    if (const auto s = Repositories::Money::summary_json(owner, "USD", 30, day)) {
        if (!(*s)["accounts"].empty() || !(*s)["not_converted"].empty()) {
            net_worth = json{{"currency", "USD"},
                             {"total", (*s)["total"]},
                             {"partial", (*s)["partial"]},
                             {"rate_date", (*s)["rate_date"]},
                             {"change_30d", (*s)["change"]["total"]},
                             {"change_partial", (*s)["change"]["partial"]}};
        }
    }
    if (currencies.is_null() && spends.empty() && net_worth.is_null()) {
        return json();
    }
    return json{{"currencies", currencies.is_null() ? json::array() : currencies},
                {"spends_today", spends},
                {"net_worth", net_worth}};
}

}  // namespace

nlohmann::json digest(const std::string& owner, const std::string& day) {
    // Cached per owner and day under the version tokens of DigestCache.hpp;
    // the answer is the same JSON either way, and Redis down means computed.
    return Cache::cached<nlohmann::json>(
        DigestCache::entry_key(owner, day), DigestCache::kTtlSec, [&] { return compute(owner, day); });
}

}  // namespace Money
