/**
 * @file Days.hpp
 * @brief Days by country from the owner's border crossings, and the counters
 *        over them (spec docs/superpowers/specs/2026-10-13-travel-design.md).
 *        Pure: no database, no clock; "today" is always passed in.
 *
 *        A day belongs to every country the owner was in that day: the country
 *        at its start and both countries of each crossing on it. Days after
 *        "today" have no record; a projection says how to treat them.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "utils/Date.hpp"

namespace Travel {

using Day = std::chrono::sys_days;

inline Day day_of(std::string_view text) {
    return Day{Utils::Date::parse_ymd(text)};
}

inline std::string text_of(Day d) {
    const std::chrono::year_month_day ymd{d};
    char buf[32];  // room for any int year: GCC checks the worst case
    std::snprintf(buf,
                  sizeof buf,
                  "%04d-%02u-%02u",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return buf;
}

struct Crossing {
    std::string date;  // YYYY-MM-DD
    std::string from;  // ISO 3166-1 alpha-2
    std::string to;
};

enum class Window { RollingDays, Rolling12MonthsInYear, SinceLastEntry };
enum class Threshold { AtMost, Over };

struct Rule {
    std::set<std::string> countries;  // concrete codes (expand SCHENGEN before)
    Window window = Window::RollingDays;
    int window_days = 0;  // RollingDays only
    int threshold_days = 0;
    Threshold kind = Threshold::AtMost;
    bool count_crossing_days = true;
};

struct Count {
    int used = 0;
    int left = 0;
    std::optional<Day> reached_on;  // a running stay meets the threshold on this day
    std::optional<Day> reentry;     // RollingDays + AtMost: the first day a full stay fits again
};

namespace detail {

constexpr int kHorizonDays = 3650;

/// The recorded crossings in date order, and where the owner was on any day.
class Timeline {
public:
    Timeline(const std::vector<Crossing>& crossings, std::string home) : home_(std::move(home)) {
        for (const auto& c : crossings) {
            items_.push_back({day_of(c.date), c.from, c.to});
        }
        std::stable_sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) { return a.day < b.day; });
        chain_each_day();
    }

    /// The country at the end of @p d: the destination of the last crossing on or before it, else home.
    std::string end_of(Day d) const {
        std::string at = home_;
        for (const auto& i : items_) {
            if (i.day > d) {
                break;
            }
            at = i.to;
        }
        return at;
    }

    /// Every country of @p d: the one at its start and both of each crossing on it.
    std::set<std::string> on(Day d) const {
        std::set<std::string> out{end_of(d - std::chrono::days{1})};
        for (const auto& i : items_) {
            if (i.day == d) {
                out.insert(i.from);
                out.insert(i.to);
            }
        }
        return out;
    }

    /// The last day on or before @p d with a crossing into @p countries from outside them.
    std::optional<Day> last_entry(const std::set<std::string>& countries, Day d) const {
        std::optional<Day> out;
        for (const auto& i : items_) {
            if (i.day > d) {
                break;
            }
            if (countries.contains(i.to) && !countries.contains(i.from)) {
                out = i.day;
            }
        }
        return out;
    }

private:
    struct Item {
        Day day;
        std::string from;
        std::string to;
    };

    /// Orders the crossings of one day as a journey: each next one leaves the country the
    /// previous one entered, starting from where the day began; the stored order breaks ties.
    void chain_each_day() {
        std::string at = home_;
        for (auto first = items_.begin(); first != items_.end();) {
            const auto last = std::find_if(first, items_.end(), [&](const Item& i) { return i.day != first->day; });
            for (auto it = first; it != last; ++it) {
                const auto next = std::find_if(it, last, [&](const Item& i) { return i.from == at; });
                if (next != last) {
                    std::rotate(it, next, next + 1);
                }
                at = it->to;
            }
            first = last;
        }
    }

    std::string home_;
    std::vector<Item> items_;
};

/// Whether a day with these countries counts for @p rule.
inline bool counts(const Rule& rule, const std::set<std::string>& on) {
    if (on.empty()) {
        return false;
    }
    const auto in = [&](const std::string& c) { return rule.countries.contains(c); };
    return rule.count_crossing_days ? std::any_of(on.begin(), on.end(), in) : std::all_of(on.begin(), on.end(), in);
}

/// How days after today are seen.
enum class After { Outside, Inside };

inline int length(Day a, Day b) {
    return a > b ? 0 : static_cast<int>((b - a).count()) + 1;
}

/// The recorded days of one rule, counted once into prefix sums over
/// [base, today], so any window costs O(1).
class Presence {
public:
    Presence(const Rule& rule, const Timeline& t, Day base, Day today) : base_(base), today_(today) {
        prefix_.push_back(0);
        for (Day d = base; d <= today; d += std::chrono::days{1}) {
            prefix_.push_back(prefix_.back() + (counts(rule, t.on(d)) ? 1 : 0));
        }
    }

    /// Days in [a, b] that count: recorded up to today (before @p stay_from),
    /// later days as @p after says, and every day from @p stay_from on.
    int between(Day a, Day b, After after, std::optional<Day> stay_from = std::nullopt) const {
        if (a > b) {
            return 0;
        }
        const Day stay = stay_from.value_or(b + std::chrono::days{1});
        const Day before_stay = std::min(b, stay - std::chrono::days{1});
        int n = recorded(a, std::min(before_stay, today_));
        if (after == After::Inside) {
            n += length(std::max(a, today_ + std::chrono::days{1}), before_stay);
        }
        n += length(std::max(a, stay), b);
        return n;
    }

private:
    int recorded(Day a, Day b) const {
        a = std::max(a, base_);
        b = std::min(b, today_);
        if (a > b) {
            return 0;
        }
        const auto i = static_cast<std::size_t>((a - base_).count());
        const auto j = static_cast<std::size_t>((b - base_).count()) + 1;
        return prefix_[j] - prefix_[i];
    }

    Day base_;
    Day today_;
    std::vector<int> prefix_;
};

/// The first day of the 12-month window ending on @p e (the day after e minus one year).
inline Day twelve_months_before(Day e) {
    std::chrono::year_month_day back = std::chrono::year_month_day{e} - std::chrono::years{1};
    if (!back.ok()) {  // Feb 29 → Feb 28
        back = std::chrono::year_month_day{back.year(), back.month(), std::chrono::day{28}};
    }
    return Day{back} + std::chrono::days{1};
}

inline Day window_start(const Rule& rule, Day end) {
    if (rule.window == Window::RollingDays) {
        return end - std::chrono::days{rule.window_days - 1};
    }
    return twelve_months_before(end);
}

/// The count at which the threshold is met: AtMost N → N (the last allowed day), Over N → N + 1.
inline int need(const Rule& rule) {
    return rule.kind == Threshold::AtMost ? rule.threshold_days : rule.threshold_days + 1;
}

/// The earliest day a counter of @p rule on @p today looks at.
inline Day earliest(const Rule& rule, Day today) {
    if (rule.window == Window::RollingDays) {
        return today - std::chrono::days{rule.window_days};
    }
    const std::chrono::year_month_day ymd{today};
    return Day{(ymd.year() - std::chrono::years{1}) / std::chrono::January / 1};
}

/// How many days a stay starting on @p start may last (RollingDays / AtMost).
inline int stay_allowed(const Rule& rule, const Presence& p, Day start) {
    int n = 0;
    for (Day k = start; n < rule.threshold_days; k += std::chrono::days{1}) {
        if (p.between(window_start(rule, k), k, After::Outside, start) > rule.threshold_days) {
            break;
        }
        ++n;
    }
    return n;
}

}  // namespace detail

/// For each day from @p from to @p to, the countries the owner was in.
inline std::map<Day, std::set<std::string>> days_by_country(const std::vector<Crossing>& crossings,
                                                            const std::string& home,
                                                            Day from,
                                                            Day to) {
    const detail::Timeline t(crossings, home);
    std::map<Day, std::set<std::string>> out;
    for (Day d = from; d <= to; d += std::chrono::days{1}) {
        out[d] = t.on(d);
    }
    return out;
}

/// How many days a stay starting on @p start may last under a RollingDays /
/// AtMost rule: days up to @p today as recorded, later days outside except the
/// stay itself. 0 for other rules.
inline int stay_allowed_from(
    const Rule& rule, const std::vector<Crossing>& crossings, const std::string& home, Day today, Day start) {
    if (rule.window != Window::RollingDays || rule.kind != Threshold::AtMost) {
        return 0;
    }
    const detail::Timeline t(crossings, home);
    const Day base = std::min(detail::earliest(rule, today), start - std::chrono::days{rule.window_days});
    const detail::Presence p(rule, t, base, today);
    return detail::stay_allowed(rule, p, start);
}

/// The counter of @p rule on @p today.
inline Count count(const Rule& rule, const std::vector<Crossing>& crossings, const std::string& home, Day today) {
    using detail::After;
    const detail::Timeline t(crossings, home);
    const bool inside = rule.countries.contains(t.end_of(today));
    const int need = detail::need(rule);
    Count out;

    if (rule.window == Window::SinceLastEntry) {
        const auto entry = t.last_entry(rule.countries, today);
        if (!inside || !entry) {
            out.left = need;
            return out;
        }
        const detail::Presence p(rule, t, *entry, today);
        out.used = p.between(*entry, today, After::Outside);
        out.left = std::max(0, need - out.used);
        // Every later day of the running stay counts: the threshold falls (need - used) days on.
        out.reached_on = out.used >= need ? today : today + std::chrono::days{need - out.used};
        return out;
    }

    const detail::Presence p(rule, t, detail::earliest(rule, today), today);
    if (rule.window == Window::RollingDays) {
        out.used = p.between(detail::window_start(rule, today), today, After::Outside);
    } else {
        // The largest 12-month count among the windows ending on a day of this year so far.
        const Day jan1{std::chrono::year_month_day{today}.year() / std::chrono::January / 1};
        for (Day e = jan1; e <= today; e += std::chrono::days{1}) {
            out.used = std::max(out.used, p.between(detail::window_start(rule, e), e, After::Outside));
        }
    }
    out.left = std::max(0, need - out.used);

    if (inside) {
        Day last = today + std::chrono::days{detail::kHorizonDays};
        if (rule.window == Window::Rolling12MonthsInYear) {
            last = Day{std::chrono::year_month_day{today}.year() / std::chrono::December / 31};
        }
        for (Day k = today; k <= last; k += std::chrono::days{1}) {
            if (p.between(detail::window_start(rule, k), k, After::Inside) >= need) {
                out.reached_on = k;
                break;
            }
        }
    }

    if (rule.window == Window::RollingDays && rule.kind == Threshold::AtMost) {
        const Day last = today + std::chrono::days{rule.window_days + rule.threshold_days};
        for (Day start = today; start <= last; start += std::chrono::days{1}) {
            if (detail::stay_allowed(rule, p, start) >= rule.threshold_days) {
                out.reentry = start;
                break;
            }
        }
    }
    return out;
}

}  // namespace Travel
