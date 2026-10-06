/**
 * @file Period.hpp
 * @brief Reporting periods as inclusive ranges of calendar days: the week
 *        (Monday to Sunday), the month and the quarter containing a day, a
 *        custom range, and the period before each.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

#include "utils/Date.hpp"

namespace Money::Period {

enum class Kind { week, month, quarter, custom };

struct Range {
    std::string from;
    std::string to;
};

namespace detail {

using std::chrono::days;
using std::chrono::sys_days;
using std::chrono::year_month_day;

inline sys_days day_of(std::string_view text) {
    return sys_days{Utils::Date::parse_ymd(text)};
}

inline std::string text_of(sys_days d) {
    const year_month_day ymd{d};
    char buf[32];  // room for any int year: GCC checks the worst case
    std::snprintf(buf,
                  sizeof buf,
                  "%04d-%02u-%02u",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return buf;
}

inline sys_days month_start(sys_days d) {
    const year_month_day ymd{d};
    return sys_days{ymd.year() / ymd.month() / 1};
}

inline sys_days quarter_start(sys_days d) {
    const year_month_day ymd{d};
    const unsigned m = static_cast<unsigned>(ymd.month());
    const unsigned first = ((m - 1) / 3) * 3 + 1;
    return sys_days{ymd.year() / std::chrono::month{first} / 1};
}

inline sys_days week_start(sys_days d) {
    const std::chrono::weekday wd{d};
    const unsigned back = (wd.c_encoding() + 6) % 7;  // Sunday is 0 in c_encoding
    return d - days{back};
}

}  // namespace detail

/// The period of `kind` containing `anchor`; `custom` is not built here.
/// @throws std::invalid_argument on a bad date or on `custom`.
inline Range of(Kind kind, std::string_view anchor) {
    using namespace detail;
    const sys_days d = day_of(anchor);
    switch (kind) {
        case Kind::week: {
            const sys_days start = week_start(d);
            return {text_of(start), text_of(start + days{6})};
        }
        case Kind::month: {
            const year_month_day ymd{d};
            const sys_days start = month_start(d);
            const sys_days next = sys_days{(ymd.year() / ymd.month() + std::chrono::months{1}) / 1};
            return {text_of(start), text_of(next - days{1})};
        }
        case Kind::quarter: {
            const sys_days start = quarter_start(d);
            const year_month_day s{start};
            const sys_days next = sys_days{(s.year() / s.month() + std::chrono::months{3}) / 1};
            return {text_of(start), text_of(next - days{1})};
        }
        case Kind::custom:
            break;
    }
    throw std::invalid_argument("a custom period needs from and to");
}

/// A custom range; @throws std::invalid_argument when `to` is before `from`.
inline Range custom(std::string_view from, std::string_view to) {
    using namespace detail;
    if (day_of(to) < day_of(from)) {
        throw std::invalid_argument("the period ends before it starts");
    }
    return {std::string(from), std::string(to)};
}

inline int days(const Range& r) {
    using namespace detail;
    return static_cast<int>((day_of(r.to) - day_of(r.from)).count()) + 1;
}

/// The period before `r`: the previous week, month or quarter, or a custom
/// range of the same length ending the day before `r.from`.
inline Range previous(Kind kind, const Range& r) {
    using detail::day_of;
    using detail::text_of;
    using D = std::chrono::days;
    const detail::sys_days start = day_of(r.from);
    switch (kind) {
        case Kind::week:
            return {text_of(start - D{7}), text_of(start - D{1})};
        case Kind::month:
        case Kind::quarter:
            return of(kind, text_of(start - D{1}));
        case Kind::custom: {
            const int len = Period::days(r);
            return {text_of(start - D{len}), text_of(start - D{1})};
        }
    }
    throw std::invalid_argument("unknown period kind");
}

/// Days of `r` from `today` to its end, inclusive; 0 when `today` is past the end,
/// the whole length when `today` is before the start.
inline int days_left(const Range& r, std::string_view today) {
    using namespace detail;
    const sys_days t = day_of(today);
    if (t > day_of(r.to)) {
        return 0;
    }
    if (t < day_of(r.from)) {
        return Period::days(r);
    }
    return static_cast<int>((day_of(r.to) - t).count()) + 1;
}

}  // namespace Money::Period
