/**
 * @file Documents.hpp
 * @brief Travel documents against the end of a trip. A passport should stay
 *        valid six months past the end (the rule many countries apply on
 *        entry, and the one the owner's Notion base used); a visa or an
 *        insurance must not end during the trip. Only each holder's latest
 *        passport counts, and a visa that ended before the trip is an old one:
 *        neither warns. Other documents are not checked.
 *
 *        Without a trip, expiring() lists the documents that run out within a
 *        number of days of today, for the Now screen: a renewal takes weeks.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <string>
#include <utility>
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

/// A document that runs out soon; @c days_left is negative once it has.
struct ExpiringDocument {
    std::string document_id;
    std::string valid_until;
    std::string holder;
    int days_left = 0;
};

/// The default of travel.documents_warn_days: six months, the passport rule.
inline constexpr int kDocumentsWarnDays = 180;

/// The documents of @p docs valid until at most @p warn_days after @p today,
/// the soonest first (then by holder). A passport or an insurance is renewed
/// by a newer one of the same holder, so only that holder's latest of the kind
/// counts, and it still warns once it has run out: nothing replaces it yet. A
/// visa or another document stands alone (two visas are two countries), and
/// one that has run out is an old one, so it is quiet. Nothing with
/// @p warn_days below zero.
inline std::vector<ExpiringDocument> expiring(const std::vector<Document>& docs,
                                              const std::string& today,
                                              int warn_days) {
    std::vector<ExpiringDocument> out;
    if (warn_days < 0) {
        return out;
    }
    const auto renewed = [](const std::string& kind) { return kind == "passport" || kind == "insurance"; };
    std::map<std::pair<std::string, std::string>, Day> latest;
    for (const auto& d : docs) {
        if (renewed(d.kind)) {
            const Day valid = day_of(d.valid_until);
            auto [it, fresh] = latest.try_emplace({d.holder, d.kind}, valid);
            if (!fresh && it->second < valid) {
                it->second = valid;
            }
        }
    }
    const Day now = day_of(today);
    for (const auto& d : docs) {
        const Day valid = day_of(d.valid_until);
        const int left = static_cast<int>((valid - now).count());
        if (left > warn_days) {
            continue;
        }
        if (renewed(d.kind) ? valid == latest.at({d.holder, d.kind}) : left >= 0) {
            out.push_back({d.id, d.valid_until, d.holder, left});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const ExpiringDocument& a, const ExpiringDocument& b) {
        return a.days_left != b.days_left ? a.days_left < b.days_left : a.holder < b.holder;
    });
    return out;
}

}  // namespace Travel
