/**
 * @file Regions.hpp
 * @brief Mi Fitness cloud regions: hosts and day boundaries.
 *
 * The region list is a set of routing candidates, not verified support:
 * upstream states explicitly that KNOWN_REGIONS guarantees nothing.
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "fitness/xiaomi/Errors.hpp"

namespace Xiaomi {

inline constexpr std::array<std::string_view, 6> kKnownRegions = {"ru", "cn", "de", "i2", "sg", "us"};

/// An empty string is a synonym for cn. Everything else is checked against the
/// candidate list: the value ends up in the host name, and an arbitrary string
/// would send the request with session cookies to a foreign domain.
inline bool is_known_region(std::string_view region) {
    if (region.empty()) {
        return true;
    }
    for (const auto candidate : kKnownRegions) {
        if (region == candidate) {
            return true;
        }
    }
    return false;
}

/// Region cn and the empty string live on hlth.io.mi.com, the rest on a subdomain.
inline std::string host_for_region(std::string_view region) {
    if (region.empty() || region == "cn") {
        return "https://hlth.io.mi.com";
    }
    return "https://" + std::string(region) + ".hlth.io.mi.com";
}

namespace detail {

/// Strict YYYY-MM-DD parsing. Anything else is a caller error, not a reason
/// to silently guess the format.
inline std::chrono::sys_days parse_date(std::string_view text) {
    const auto bad = [&] { return MiFitnessProtocolError("date must be YYYY-MM-DD and a real calendar day"); };
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        throw bad();
    }
    for (const std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u}) {
        if (text[i] < '0' || text[i] > '9') {
            throw bad();
        }
    }
    const auto number = [&](std::size_t from, std::size_t count) {
        int value = 0;
        for (std::size_t i = from; i < from + count; ++i) {
            value = value * 10 + (text[i] - '0');
        }
        return value;
    };
    const std::chrono::year_month_day date{std::chrono::year(number(0, 4)),
                                           std::chrono::month(static_cast<unsigned>(number(5, 2))),
                                           std::chrono::day(static_cast<unsigned>(number(8, 2)))};
    if (!date.ok()) {
        throw bad();
    }
    return std::chrono::sys_days(date);
}

}  // namespace detail

/**
 * @brief Date range boundaries in epoch seconds, inclusive on both ends.
 *
 * Region cn counts days in a fixed UTC+8, the rest in UTC: that is what
 * upstream does, and a mistake here shifts the day and corrupts daily aggregates.
 */
inline std::pair<std::int64_t, std::int64_t> range_to_timestamps(std::string_view start_date,
                                                                 std::string_view end_date,
                                                                 std::string_view region) {
    const std::int64_t offset = (region.empty() || region == "cn") ? 8 * 3600 : 0;
    const std::int64_t start_days = detail::parse_date(start_date).time_since_epoch().count();
    const std::int64_t end_days = detail::parse_date(end_date).time_since_epoch().count();
    if (start_days > end_days) {
        throw MiFitnessProtocolError("start date is after end date");
    }
    return {start_days * 86400 - offset, end_days * 86400 + 86399 - offset};
}

}  // namespace Xiaomi
