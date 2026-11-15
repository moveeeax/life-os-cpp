/**
 * @file Date.hpp
 * @brief The one "YYYY-MM-DD" parser and formatter of the app: strict shape,
 *        a real calendar day, nothing else. Dates travel as text everywhere
 *        (JSON, SQL), and this is where they become a day and back.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Utils::Date {

/// @throws std::invalid_argument on a bad shape or a day that does not exist.
inline std::chrono::year_month_day parse_ymd(std::string_view text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        throw std::invalid_argument("date must be YYYY-MM-DD");
    }
    const auto num = [&](std::size_t at, std::size_t len) {
        int v = 0;
        for (std::size_t i = at; i < at + len; ++i) {
            if (text[i] < '0' || text[i] > '9') {
                throw std::invalid_argument("date must be YYYY-MM-DD");
            }
            v = v * 10 + (text[i] - '0');
        }
        return v;
    };
    const std::chrono::year_month_day ymd{std::chrono::year(num(0, 4)),
                                          std::chrono::month(static_cast<unsigned>(num(5, 2))),
                                          std::chrono::day(static_cast<unsigned>(num(8, 2)))};
    if (!ymd.ok()) {
        throw std::invalid_argument("date is not a calendar day");
    }
    return ymd;
}

/// Whether @p text is YYYY-MM-DD naming a day that exists: "2026-02-30" is not.
inline bool is_ymd(std::string_view text) {
    try {
        parse_ymd(text);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

/// @p d as YYYY-MM-DD (the year zero-padded to four digits).
inline std::string format_ymd(std::chrono::year_month_day d) {
    char buf[32];  // room for any int year: GCC checks the worst case
    std::snprintf(buf,
                  sizeof buf,
                  "%04d-%02u-%02u",
                  static_cast<int>(d.year()),
                  static_cast<unsigned>(d.month()),
                  static_cast<unsigned>(d.day()));
    return buf;
}

/// @p d as YYYY-MM-DD.
inline std::string format_ymd(std::chrono::sys_days d) {
    return format_ymd(std::chrono::year_month_day{d});
}

/// Today's date in UTC as YYYY-MM-DD.
inline std::string today_utc() {
    return format_ymd(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
}

/// The YYYY-MM-DD day @p n days after @p ymd (before it when @p n is negative).
/// @throws std::invalid_argument when @p ymd is not YYYY-MM-DD (parse_ymd).
inline std::string add_days(std::string_view ymd, int n) {
    return format_ymd(std::chrono::sys_days{parse_ymd(ymd)} + std::chrono::days{n});
}

}  // namespace Utils::Date
