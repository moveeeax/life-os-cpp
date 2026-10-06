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

inline std::string url_for(const std::string& date_or_latest) {
    return "https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@" + date_or_latest + "/v1/currencies/usd.json";
}

/// @param date_or_latest "latest" or "YYYY-MM-DD".
/// @throws Unavailable, NoSnapshot.
Day fetch(Net::Http::Transport& transport, const std::string& date_or_latest);

}  // namespace Money::RatesSource
