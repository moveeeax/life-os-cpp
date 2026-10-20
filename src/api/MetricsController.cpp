/**
 * @file MetricsController.cpp
 * @brief The /api/v1/metrics handlers: the catalog expanded for the caller, and
 *        one series by local day. A series the caller cannot read answers 200
 *        with no days and the reason in `why`, never another user's rows.
 */

#include "api/MetricsController.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "domain/Role.hpp"
#include "habits/Habits.hpp"
#include "metrics/Catalog.hpp"
#include "repositories/Zones.hpp"
#include "repositories/metrics/Series.hpp"
#include "security/Auth.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Metrics::Why;

/// The most days one series read answers.
constexpr int kRangeMax = 400;

/// The caller's permission bits, the fitness read bit set when the caller passes
/// that check (an admin, or no auth enforced).
std::uint32_t permissions_of(const HttpRequestPtr& req) {
    std::uint32_t p = Security::Auth::current_permissions(req);
    if (Security::Auth::require_permission(req, Domain::Permission::kFitnessRead) == nullptr) {
        p |= Domain::Permission::kFitnessRead;
    }
    return p;
}

json why_json(Why w) {
    return w == Why::Ok ? json() : json(std::string(Repositories::Metrics::why_name(w)));
}

json entry(const ::Metrics::Ref& r, const std::string& label, const std::string& unit, Why why) {
    return {{"id", ::Metrics::canonical(r)},
            {"module", std::string(r.def->module)},
            {"label", label},
            {"unit", unit},
            {"reduce", std::string(::Metrics::reduce_name(r.def->reduce))},
            {"presence", r.def->presence},
            {"available", why == Why::Ok},
            {"why", why_json(why)}};
}

}  // namespace

void MetricsController::list(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::goals_enabled() || Core::habits_enabled(), "metrics", req, callback, owner);
    const std::uint32_t permissions = permissions_of(req);
    with_repo_errors(callback, "metrics.list", [&] {
        const auto access = Repositories::Metrics::access_of(owner, permissions);
        const auto choices = Repositories::Metrics::choices_of(owner);
        json out = json::array();
        for (const auto& def : ::Metrics::catalog()) {
            // The parameters below are the owner's own: only the gate can stop them.
            const Why why = Repositories::Metrics::gate_available(def, access);
            const std::string label(def.label);
            ::Metrics::Ref r;
            r.def = &def;
            switch (def.param) {
                case ::Metrics::Param::None:
                    out.push_back(entry(r, label, std::string(def.unit), why));
                    break;
                case ::Metrics::Param::Currency: {
                    const auto& currencies =
                        def.gate == ::Metrics::Gate::Money ? choices.money_currencies : choices.practice_currencies;
                    for (const auto& c : currencies) {
                        r.currency = c;
                        out.push_back(entry(r, label + ", " + c, c, why));
                    }
                    break;
                }
                case ::Metrics::Param::CurrencyCategory:
                    for (const auto& c : choices.money_currencies) {
                        for (const auto& cat : choices.categories) {
                            r.currency = c;
                            r.category_id = cat.id;
                            out.push_back(entry(r, "Expenses: " + cat.name + ", " + c, c, why));
                        }
                    }
                    break;
                case ::Metrics::Param::Habit:
                    for (const auto& h : choices.habits) {
                        r.habit_id = h.id;
                        out.push_back(entry(r, h.name, h.unit, why));
                    }
                    break;
            }
        }
        callback(Response::ok(json{{"data", out}}));
    });
}

void MetricsController::series(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::goals_enabled() || Core::habits_enabled(), "metrics", req, callback, owner);
    const auto ref = ::Metrics::parse(req->getParameter("source"));
    if (!ref) {
        callback(ErrorResponse::bad_request("invalid_query", "source must be an id of the metrics catalog"));
        return;
    }
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    if (!Fields::is_calendar_date(from) || !Fields::is_calendar_date(to) || to < from) {
        callback(ErrorResponse::bad_request("invalid_query", "from and to must be calendar days, from first"));
        return;
    }
    const auto a = ::Habits::day_of(from);
    const auto b = ::Habits::day_of(to);
    if ((b - a).count() >= kRangeMax) {
        callback(ErrorResponse::bad_request("invalid_query", "at most 400 days at a time"));
        return;
    }
    const std::string tz = req->getParameter("tz").empty() ? "UTC" : req->getParameter("tz");
    const std::uint32_t permissions = permissions_of(req);
    with_repo_errors(callback, "metrics.series", [&] {
        if (!Repositories::Zones::is_zone(tz)) {
            callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
            return;
        }
        const auto access = Repositories::Metrics::access_of(owner, permissions);
        const auto s = Repositories::Metrics::read(owner, *ref, a, b, tz, access);
        json data = json::array();
        for (const auto& [day, value] : s.values) {
            data.push_back({{"date", ::Habits::text_of(day)}, {"value", std::isnan(value) ? json() : json(value)}});
        }
        callback(Response::ok(
            json{{"source", ::Metrics::canonical(*ref)},
                 {"unit", s.why == Why::Ok ? Repositories::Metrics::unit_for(owner, *ref) : ::Metrics::unit_of(*ref)},
                 {"why", why_json(s.why)},
                 {"data", data}}));
    });
}

}  // namespace Api
