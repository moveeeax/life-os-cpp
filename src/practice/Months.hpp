/**
 * @file Months.hpp
 * @brief The calendar words of the practice module: a month as "YYYY-MM" and
 *        the half-open day range it covers, a year as "YYYY", and the stages
 *        of a deal in the order the lists show them. Pure: no database, no I/O.
 */

#pragma once

#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace Practice {

/// The stages of a deal, open ones first: the order of the deal list.
inline constexpr std::array<std::string_view, 5> kStages = {"lead", "negotiation", "active", "done", "lost"};

inline bool is_stage(std::string_view s) {
    for (const auto& k : kStages) {
        if (k == s) {
            return true;
        }
    }
    return false;
}

namespace Months {

/// The days of one month: [first, next) as YYYY-MM-DD.
struct Range {
    std::string first;
    std::string next;
};

namespace detail {

inline std::optional<int> digits(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    int v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        v = v * 10 + (c - '0');
    }
    return v;
}

inline std::string day_one(int year, int month) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-01", year, month);
    return buf;
}

}  // namespace detail

/// "2026" → 2026 for a year in 1900..2100; nullopt for anything else.
inline std::optional<int> year_of(std::string_view text) {
    if (text.size() != 4) {
        return std::nullopt;
    }
    const auto y = detail::digits(text);
    if (!y || *y < 1900 || *y > 2100) {
        return std::nullopt;
    }
    return y;
}

/// "2026-10" → {"2026-10-01", "2026-11-01"}; "2026-12" ends on the next year's
/// first day. nullopt for anything that is not a month of 1900..2100.
inline std::optional<Range> parse(std::string_view text) {
    if (text.size() != 7 || text[4] != '-') {
        return std::nullopt;
    }
    const auto y = year_of(text.substr(0, 4));
    const auto m = detail::digits(text.substr(5, 2));
    if (!y || !m || *m < 1 || *m > 12) {
        return std::nullopt;
    }
    const int next_year = *m == 12 ? *y + 1 : *y;
    const int next_month = *m == 12 ? 1 : *m + 1;
    return Range{detail::day_one(*y, *m), detail::day_one(next_year, next_month)};
}

/// The days of one year: [YYYY-01-01, YYYY+1-01-01).
inline Range year_range(int year) {
    return Range{detail::day_one(year, 1), detail::day_one(year + 1, 1)};
}

}  // namespace Months

}  // namespace Practice
