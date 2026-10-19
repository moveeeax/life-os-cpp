/**
 * @file RatesSource.hpp
 * @brief The one rates source of the money module: fawazahmed0/currency-api
 *        on jsDelivr, one GET for every quote against the US dollar, `latest`
 *        or a dated snapshot. Not a bank's rate; a daily average of public
 *        sources, enough for an "as if in KZT" caption.
 */

#pragma once

#include <map>
#include <stdexcept>
#include <string>

#include "net/Http.hpp"

namespace Money::RatesSource {

struct Day {
    std::string date;                       // the snapshot's own date
    std::map<std::string, double> per_usd;  // upper-case codes
};

/// The source answered with no usable day (a transport failure, a 5xx, a body
/// that is not the expected JSON). The queue retries.
struct Unavailable : std::runtime_error {
    explicit Unavailable(const std::string& what) : std::runtime_error(what) {}
};

/// No snapshot exists for that date (a 404 on a dated URL); nothing to retry.
struct NoSnapshot : std::runtime_error {
    explicit NoSnapshot(const std::string& date) : std::runtime_error("no rates snapshot for " + date) {}
};

/// True for "latest" or a calendar-shaped "YYYY-MM-DD" (month 01-12, day
/// 01-31). The value is spliced into the jsDelivr path, so anything else
/// ("../", a query, another package) is refused before a URL is built.
inline bool valid_date_arg(const std::string& s) {
    if (s == "latest")
        return true;
    if (s.size() != 10 || s[4] != '-' || s[7] != '-')
        return false;
    for (const std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
        if (s[i] < '0' || s[i] > '9')
            return false;
    }
    const int month = (s[5] - '0') * 10 + (s[6] - '0');
    const int day = (s[8] - '0') * 10 + (s[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

inline std::string url_for(const std::string& date_or_latest) {
    return "https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@" + date_or_latest + "/v1/currencies/usd.json";
}

/// @param date_or_latest "latest" or "YYYY-MM-DD".
/// @throws std::invalid_argument when date_or_latest fails valid_date_arg
///         (no request is made); Unavailable, NoSnapshot.
Day fetch(Net::Http::Transport& transport, const std::string& date_or_latest);

}  // namespace Money::RatesSource
