/**
 * @file Documents.hpp
 * @brief Travel documents against the end of a trip. A passport should stay
 *        valid six months past the end (the rule many countries apply on
 *        entry, and the one the owner's Notion base used); a visa or an
 *        insurance must not end during the trip. Only each holder's latest
 *        passport counts, and a visa that ended before the trip is an old one:
 *        neither warns. Other documents are not checked.
 */

#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "travel/Days.hpp"

namespace Travel {

struct Document {
    std::string id;
    std::string holder;
    std::string kind;         // passport | visa | insurance | other
    std::string valid_until;  // YYYY-MM-DD
};

struct DocumentWarning {
    std::string document_id;
    std::string reason;  // passport_6_months | expires_during_trip
};

inline constexpr int kPassportMonths = 6;

/// @p end + six months; a day past the end of the target month falls back to its last day.
inline Day six_months_after(Day end) {
    using namespace std::chrono;
    const year_month_day ymd{end};
    const year_month target = year_month{ymd.year(), ymd.month()} + months{kPassportMonths};
    const day last = year_month_day_last{target.year(), month_day_last{target.month()}}.day();
    return sys_days{target.year() / target.month() / (ymd.day() > last ? last : ymd.day())};
}

/// The documents that do not last for a trip from @p start to @p end, in their order.
inline std::vector<DocumentWarning> warnings(const std::vector<Document>& docs,
                                             const std::string& start,
                                             const std::string& end) {
    const Day first = day_of(start);
    const Day last = day_of(end);
    const Day passport_until = six_months_after(last);
    std::map<std::string, Day> latest_passport;
    for (const auto& d : docs) {
        if (d.kind == "passport") {
            const Day valid = day_of(d.valid_until);
            auto [it, fresh] = latest_passport.try_emplace(d.holder, valid);
            if (!fresh && it->second < valid) {
                it->second = valid;
            }
        }
    }
    std::vector<DocumentWarning> out;
    for (const auto& d : docs) {
        const Day valid = day_of(d.valid_until);
        if (d.kind == "passport" && valid == latest_passport.at(d.holder) && valid < passport_until) {
            out.push_back({d.id, "passport_6_months"});
        } else if ((d.kind == "visa" || d.kind == "insurance") && first <= valid && valid < last) {
            out.push_back({d.id, "expires_during_trip"});
        }
    }
    return out;
}

}  // namespace Travel
