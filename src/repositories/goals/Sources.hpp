/**
 * @file Sources.hpp
 * @brief The source of a number goal (goal_items.source, an id of the metrics
 *        catalog) read for one request: the series over the goal's days merged
 *        with its check-ins, and why the series cannot be read when it cannot
 *        (docs/superpowers/specs/2026-10-20-metric-sources-design.md §3, §4).
 *        An unreadable source is never an error: the check-ins alone drive
 *        the progress then.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "goals/Progress.hpp"
#include "habits/Habits.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/metrics/Series.hpp"

namespace Repositories::Goals {

class Sources {
public:
    /// The most days one series read covers; a longer goal reads in windows.
    static constexpr int kWindowDays = 400;

    /**
     * @param tz          The IANA zone the days of the series are counted in (checked by the caller).
     * @param permissions The caller's Domain::Permission bits (the fitness read bit gates the Mi series).
     */
    Sources(std::string owner, std::string tz, std::uint32_t permissions)
        : owner_(std::move(owner)), tz_(std::move(tz)), permissions_(permissions) {}

    struct Read {
        std::vector<::Goals::Progress::Point> points;
        /// null when the goal has no source or the source is readable, else
        /// module_off, no_permission, not_linked or not_yours.
        nlohmann::json why;
    };

    /**
     * The points of the goal @p row (a GoalRepository row) up to @p date: the days
     * of its source from start_date to @p date merged with @p checkins, a check-in
     * winning on its date. A goal without a source has its check-ins only.
     */
    Read points(const nlohmann::json& row,
                const std::string& date,
                const std::vector<::Goals::Progress::Checkin>& checkins) {
        Read out;
        std::map<std::string, double> series;
        const auto source = row.find("source");
        if (source != row.end() && source->is_string()) {
            const Repositories::Metrics::Why why = read_into(series, source->get<std::string>(), row, date);
            if (why != Repositories::Metrics::Why::Ok) {
                out.why = std::string(Repositories::Metrics::why_name(why));
            }
        }
        out.points = ::Goals::Progress::merge(series, checkins);
        return out;
    }

    /// The points as the goal answer lists them: date, value, and where the value came from.
    static nlohmann::json points_json(const std::vector<::Goals::Progress::Point>& points) {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& p : points) {
            out.push_back({{"date", p.date}, {"value", p.value}, {"from", p.from_source ? "source" : "checkin"}});
        }
        return out;
    }

private:
    const Repositories::Metrics::Access& access() {
        if (!access_) {
            access_ = Repositories::Metrics::access_of(owner_, permissions_);
        }
        return *access_;
    }

    /// Reads the source @p id over the goal's days into @p series (date -> value).
    Repositories::Metrics::Why read_into(std::map<std::string, double>& series,
                                         const std::string& id,
                                         const nlohmann::json& row,
                                         const std::string& date) {
        using Repositories::Metrics::Why;
        const auto ref = ::Metrics::parse(id);
        if (!ref) {
            // The controller stores catalog ids only; an id the catalog no longer knows reads as nothing of the
            // caller's.
            return Why::NotYours;
        }
        const Why why = Repositories::Metrics::available(owner_, *ref, access());
        if (why != Why::Ok) {
            return why;
        }
        const ::Habits::Day start = ::Habits::day_of(row.at("start_date").get<std::string>());
        const ::Habits::Day end = ::Habits::day_of(date);
        for (::Habits::Day lo = start; lo <= end; lo += std::chrono::days{kWindowDays}) {
            const ::Habits::Day hi = std::min(end, lo + std::chrono::days{kWindowDays - 1});
            const auto s = Repositories::Metrics::read(owner_, *ref, lo, hi, tz_, access());
            if (s.why != Why::Ok) {
                return s.why;
            }
            for (const auto& [day, value] : s.values) {
                series[::Habits::text_of(day)] = value;
            }
        }
        return Why::Ok;
    }

    std::string owner_;
    std::string tz_;
    std::uint32_t permissions_;
    std::optional<Repositories::Metrics::Access> access_;
};

}  // namespace Repositories::Goals
