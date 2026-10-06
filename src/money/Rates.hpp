/**
 * @file Rates.hpp
 * @brief Converting between two currencies through their quotes against the
 *        US dollar. The result is a number for a caption ("as if in KZT, rate
 *        of 2026-10-03"); nothing of it is stored.
 */

#pragma once

#include <optional>
#include <string>

namespace Money::Rates {

struct Rate {
    std::string date;    // the day the quote is from
    std::string quote;   // ISO code
    double per_usd = 0;  // units of `quote` per 1 USD; USD itself is 1
};

/// Units of `to` per 1 `from`; nullopt when a quote is missing or 0.
inline std::optional<double> cross(const Rate& from, const Rate& to) {
    if (from.per_usd <= 0 || to.per_usd <= 0) {
        return std::nullopt;
    }
    return to.per_usd / from.per_usd;
}

struct Converted {
    double amount = 0;
    std::string rate_date;  // the older of the two quotes' days
};

/// `amount` of `from` as `to`; nullopt when either rate is missing.
inline std::optional<Converted> convert(double amount, const Rate* from, const Rate* to) {
    if (from == nullptr || to == nullptr) {
        return std::nullopt;
    }
    const auto factor = cross(*from, *to);
    if (!factor.has_value()) {
        return std::nullopt;
    }
    return Converted{amount * *factor, std::min(from->date, to->date)};
}

}  // namespace Money::Rates
