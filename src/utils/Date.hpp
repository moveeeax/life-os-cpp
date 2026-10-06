/**
 * @file Date.hpp
 * @brief The one "YYYY-MM-DD" parser of the app: strict shape, a real
 *        calendar day, nothing else. Dates travel as text everywhere (JSON,
 *        SQL), and this is where they become a day.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <stdexcept>
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

}  // namespace Utils::Date
