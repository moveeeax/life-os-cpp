/**
 * @file RequestUtils.hpp
 * @brief Small request-parsing helpers shared by controllers and middleware.
 * @details Everything here is `inline` with external linkage — these used to
 *          live in an anonymous namespace inside Api.hpp, which is an ODR
 *          trap for inline callers (internal-linkage entities referenced from
 *          inline functions make the definitions differ across TUs).
 *
 *          The drogon-free half (is_valid_uuid, normalize_path_for_metrics)
 *          lives in PathNormalize.hpp, re-exported here — the
 *          fuzz_path_match harness (tests/fuzz) includes that header
 *          without pulling <drogon/HttpRequest.h>.
 */

#pragma once

#include <exception>
#include <functional>
#include <optional>
#include <string>

#include <drogon/HttpRequest.h>

#include "api/PathNormalize.hpp"

namespace Api {

/**
 * @brief Parse a query-param string to int, returning @p def on empty/invalid.
 */
inline int parse_int(const std::string& s, int def) {
    if (s.empty())
        return def;
    try {
        return std::stoi(s);
    } catch (...) {
        return def;
    }
}

/**
 * @brief parse_int + clamp into [lo, hi]. Shared by list endpoints that take
 *        a `limit`/`offset` query param.
 */
inline int clamp_int(const std::string& s, int def, int lo, int hi) {
    int v = parse_int(s, def);
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/**
 * @brief Parsed limit/offset pair for offset-paginated list endpoints.
 *        One semantics everywhere: out-of-range values CLAMP into the
 *        documented bounds (they don't silently reset to the default).
 */
struct PageParams {
    int limit = 20;
    int offset = 0;
};

inline PageParams parse_page_params(const drogon::HttpRequestPtr& req, int default_limit, int max_limit) {
    PageParams p;
    p.limit = clamp_int(req->getParameter("limit"), default_limit, 1, max_limit);
    p.offset = clamp_int(req->getParameter("offset"), 0, 0, 1000000);
    return p;
}

/// Today in an IANA zone as YYYY-MM-DD, nullopt when the name is not a zone.
/// Controllers pass Repositories::Zones::today_in (the database's clock and
/// zone rules); tests pass a fixed answer.
using ZoneToday = std::function<std::optional<std::string>(const std::string& tz)>;

/**
 * @brief The caller's today: today in ?tz= when it names a zone, else
 *        @p utc_today. A user east of UTC is already in tomorrow for hours
 *        while UTC is not; a default taken from UTC would file their morning
 *        under yesterday. A tz that is not a zone falls back to UTC here; a
 *        controller that refuses it says so itself.
 */
inline std::string local_today(const drogon::HttpRequestPtr& req,
                               const ZoneToday& zone_today,
                               const std::string& utc_today) {
    const std::string tz = req->getParameter("tz");
    if (!tz.empty()) {
        try {
            if (auto day = zone_today(tz)) {
                return *day;
            }
        } catch (const std::exception&) {
            // The resolver could not answer (the database is away): the day falls back to UTC, and
            // the handler's own queries report the outage where its error mapping is.
        }
    }
    return utc_today;
}

/// The day a request is about: ?date= as given (the caller checks it), else local_today.
inline std::string request_day(const drogon::HttpRequestPtr& req,
                               const ZoneToday& zone_today,
                               const std::string& utc_today) {
    const std::string date = req->getParameter("date");
    return date.empty() ? local_today(req, zone_today, utc_today) : date;
}

}  // namespace Api
