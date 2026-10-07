/**
 * @file MoneyRatesHandler.hpp
 * @brief Worker job `money_rates`: fetch one day's quotes against the US
 *        dollar from the rates source and store the ones any user needs.
 *        Payload `{date?}`: "latest" (the default) or a "YYYY-MM-DD" snapshot
 *        for the backfill. Idempotent: a day already stored is written again
 *        with the same numbers.
 *
 *        A source outage throws (the queue retries); a dated day the source
 *        never published finishes with `no_snapshot`, nothing to retry.
 */

#pragma once

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "core/Modules.hpp"
#include "jobs/Jobs.hpp"
#include "money/Period.hpp"
#include "money/RatesSource.hpp"
#include "net/Http.hpp"
#include "repositories/money/FxRateRepository.hpp"

namespace Jobs::MoneyRates {

inline constexpr const char* kJobType = "money_rates";

inline nlohmann::json process_job(const nlohmann::json& payload) {
    const std::string date = payload.value("date", std::string("latest"));
    if (!Core::money_enabled()) {
        return {{"status", "skipped"}, {"reason", "money_disabled"}, {"date", date}};
    }
    Repositories::Money::FxRateRepository rates;
    const std::set<std::string> needed = rates.quotes_needed();
    Money::RatesSource::Day day;
    try {
        day = Money::RatesSource::fetch(Net::Http::transport(), date);
    } catch (const Money::RatesSource::NoSnapshot&) {
        spdlog::info("money rates: no snapshot for {}", date);
        return {{"status", "no_snapshot"}, {"date", date}};
    } catch (const Money::RatesSource::Unavailable& e) {
        // The queue retries with its backoff.
        throw std::runtime_error(std::string("money_rates: ") + e.what());
    }
    std::map<std::string, double> keep;
    std::set<std::string> missing;
    for (const auto& quote : needed) {
        const auto it = day.per_usd.find(quote);
        if (it == day.per_usd.end()) {
            missing.insert(quote);
        } else {
            keep[quote] = it->second;
        }
    }
    rates.put_day(day.date, keep);
    nlohmann::json out{{"status", "done"}, {"date", day.date}, {"stored", keep.size()}};
    if (!missing.empty()) {
        out["missing"] = missing;
        spdlog::warn("money rates {}: {} quote(s) the source lacks", day.date, missing.size());
    }
    return out;
}

/// The days in [from, to] that have no rates yet, oldest first.
inline std::vector<std::string> missing_days(const std::string& from, const std::string& to) {
    using namespace Money::Period::detail;
    Repositories::Money::FxRateRepository rates;
    std::vector<std::string> out;
    for (sys_days d = day_of(from); d <= day_of(to); d += std::chrono::days{1}) {
        std::string date = text_of(d);
        if (!rates.has_day(date)) {
            out.push_back(std::move(date));
        }
    }
    return out;
}

/// Rate jobs still waiting in the queue (a running one is not counted).
inline long waiting() {
    const auto depth = Jobs::get().queue_depth_by_type();
    const auto it = depth.find(kJobType);
    return it == depth.end() ? 0 : it->second;
}

/// One job per day. Returns how many were enqueued.
inline int enqueue_days(const std::vector<std::string>& days) {
    for (const auto& date : days) {
        Jobs::get().submit(kJobType, nlohmann::json{{"date", date}});
    }
    return static_cast<int>(days.size());
}

}  // namespace Jobs::MoneyRates
