/**
 * @file AniCuesController.cpp
 * @brief Body for src/api/AniCuesController.hpp.
 */

#include "api/AniCuesController.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "agent/Cues.hpp"
#include "api/CalendarCache.hpp"
#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "calendar/Access.hpp"
#include "calendar/Google.hpp"
#include "domain/Role.hpp"
#include "fitness/now/Digest.hpp"
#include "money/Period.hpp"
#include "net/Http.hpp"
#include "repositories/Zones.hpp"
#include "repositories/agent/CueRepository.hpp"
#include "repositories/calendar/LinkRepository.hpp"
#include "repositories/fitness/TapeRepository.hpp"
#include "repositories/food/EntryRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/ReportBuilder.hpp"
#include "security/Auth.hpp"
#include "utils/BlockingPool.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Agent::CueRepository;
namespace Cues = ::Agent::Cues;

#define ANI_CUES_GUARD(req, callback, owner) \
    API_REQUIRE_USER(req, callback, owner);  \
    API_REFUSE_API_KEY(req, callback)

/// One Google call of the cues at most this long (a token refresh and the events: two calls).
constexpr long kCalendarCallSeconds = 4;

/// ?date= and ?tz= of a request: UTC without a zone, today in the zone without a date;
/// nullopt (answered 400) when either names nothing.
std::optional<std::pair<std::string, std::string>> day_of(const HttpRequestPtr& req,
                                                          const AniCuesController::Callback& callback) {
    std::string tz = req->getParameter("tz");
    if (tz.empty()) {
        tz = "UTC";
    }
    if (!Repositories::Zones::is_zone(tz)) {
        callback(ErrorResponse::bad_request("invalid_query", "tz must be an IANA zone like Asia/Almaty"));
        return std::nullopt;
    }
    std::string date = req->getParameter("date");
    if (date.empty()) {
        date = Repositories::Zones::today_in(tz).value_or(Fields::today_utc());
    }
    if (!Fields::is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day as YYYY-MM-DD"));
        return std::nullopt;
    }
    return std::make_pair(date, tz);
}

/// Runs one source of the cues; a source that throws stays quiet, never fails the whole.
template <typename Fn>
void source(const char* name, Fn&& fn) {
    try {
        fn();
    } catch (const std::exception& e) {
        spdlog::warn("ani.cues: the {} source failed: {}", name, e.what());
    }
}

/// The meals with an entry on @p date.
std::set<std::string> meals_of(const std::string& owner, const std::string& date) {
    std::set<std::string> out;
    const json day = Repositories::EntryRepository().day(owner, date);
    if (day.contains("meals") && day["meals"].is_object()) {
        for (auto it = day["meals"].begin(); it != day["meals"].end(); ++it) {
            if (it.value().is_array() && !it.value().empty()) {
                out.insert(it.key());
            }
        }
    }
    return out;
}

/// The month's categories in the order the Money screen lists them: the
/// currencies by their spending, each one's categories by theirs.
std::vector<Cues::Category> month_of(const std::string& owner, const std::string& date) {
    using ::Money::Period::Kind;
    const json report = Repositories::Money::ReportBuilder::build(
        owner, Kind::month, "month", ::Money::Period::of(Kind::month, date), date, "");
    std::map<std::string, std::string> names;
    for (const auto& c : Repositories::Money::CategoryRepository().list(owner, true)) {
        names[c.value("id", std::string())] = c.value("name", std::string());
    }
    std::vector<json> blocks(report["blocks"].begin(), report["blocks"].end());
    std::stable_sort(blocks.begin(), blocks.end(), [](const json& a, const json& b) {
        return a.value("expense", 0.0) > b.value("expense", 0.0);
    });
    std::vector<Cues::Category> out;
    for (const auto& b : blocks) {
        std::vector<json> cats(b["categories"].begin(), b["categories"].end());
        std::stable_sort(cats.begin(), cats.end(), [](const json& x, const json& y) {
            return x.value("spent", 0.0) > y.value("spent", 0.0);
        });
        for (const auto& c : cats) {
            const std::string id = c["category_id"].is_string() ? c["category_id"].get<std::string>() : "";
            const auto name = names.find(id);
            Cues::Category cat;
            cat.title = name != names.end() ? name->second : "Без категории";
            cat.currency = b.value("currency", std::string());
            cat.spent = c.value("spent", 0.0);
            if (c["budget"].is_number()) {
                cat.budget = c["budget"].get<double>();
            }
            out.push_back(std::move(cat));
        }
    }
    return out;
}

/// The day's timed meetings: minutes from now to each start, in order of the start.
std::vector<int> meetings_of(const std::vector<Calendar::Event>& events, const CueRepository::Clock& clock) {
    std::vector<long long> starts;
    for (const auto& e : events) {
        if (!e.all_day && e.start >= clock.from && e.start < clock.to) {
            starts.push_back(e.start);
        }
    }
    std::sort(starts.begin(), starts.end());
    std::vector<int> out;
    for (const long long s : starts) {
        out.push_back(static_cast<int>(std::llround(static_cast<double>(s - clock.now) / 60.0)));
    }
    return out;
}

/// The cues of the day, the dismissed ones left out: the rules' first, then the agent's.
json answer(const Cues::Input& in,
            const std::string& expires_at,
            const std::set<std::string>& dismissed,
            const json& agent_rows) {
    std::vector<Cues::Cue> all = Cues::rules(in);
    for (const auto& row : agent_rows) {
        all.push_back(Cues::from_row(row));
    }
    json data = json::array();
    for (const auto& c : all) {
        if (dismissed.count(c.id) == 0) {
            data.push_back(Cues::to_json(c, expires_at));
        }
    }
    return json{{"data", data}};
}

}  // namespace

void AniCuesController::list(const HttpRequestPtr& req, Callback&& callback) {
    ANI_CUES_GUARD(req, callback, owner);
    const auto day = day_of(req, callback);
    if (!day) {
        return;
    }
    const std::string date = day->first;
    const std::string tz = day->second;
    const bool fitness_read = Security::Auth::require_permission(req, Domain::Permission::kFitnessRead) == nullptr;

    CueRepository cues;
    CueRepository::Clock clock;
    std::set<std::string> dismissed;
    json agent_rows = json::array();
    try {
        clock = cues.clock(date, tz);
        dismissed = cues.dismissed(owner, date);
        agent_rows = cues.of_day(owner, date);
    } catch (const std::exception& e) {
        spdlog::error("ani.cues failed: {}", e.what());
        callback(ErrorResponse::internal_error());
        return;
    }

    Cues::Input in;
    in.date = date;
    in.minute = date == clock.today ? clock.minute : date < clock.today ? Cues::kDayOver : Cues::kDayAhead;
    if (Core::food_enabled()) {
        source("food", [&] { in.meals = meals_of(owner, date); });
    }
    if (Core::fitness_enabled()) {
        source("tape", [&] {
            Repositories::Tape::TapeRepository::Filter f;
            f.to = date;
            f.limit = 1;
            const json rows = Repositories::Tape::TapeRepository().list(owner, f)["data"];
            if (!rows.empty() && rows[0]["date"].is_string()) {
                in.last_measure = rows[0]["date"].get<std::string>();
            }
            in.body = true;
        });
        if (fitness_read) {
            source("sleep", [&] {
                const json health = ::Fitness::digest(owner, date);
                if (health.is_object() && health["sleep_minutes"].is_number()) {
                    in.sleep_minutes = static_cast<int>(std::llround(health["sleep_minutes"].get<double>()));
                }
            });
        }
    }
    if (Core::money_enabled()) {
        source("money", [&] { in.money = month_of(owner, date); });
    }

    // The calendar is asked only when its rule could speak: lunch time, lunch not written.
    const bool want_meetings = in.meals && in.meals->count("lunch") == 0 && in.minute >= 11 * 60 && in.minute < 16 * 60;
    const auto reply = [expires_at = clock.expires_at, dismissed, agent_rows](const Callback& cb,
                                                                              const Cues::Input& input) {
        cb(Response::ok(answer(input, expires_at, dismissed, agent_rows)));
    };
    if (!want_meetings || !Core::now_enabled() || !Calendar::settings()) {
        reply(callback, in);
        return;
    }
    try {
        const auto link = Repositories::Calendar::LinkRepository().get(owner);
        if (!link || link->broken) {
            reply(callback, in);
            return;
        }
        if (const auto kept = CalendarCache::get(owner, clock.from, clock.to)) {
            in.meetings = meetings_of(*kept, clock);
            reply(callback, in);
            return;
        }
        if (CalendarCache::down(owner)) {
            reply(callback, in);
            return;
        }
    } catch (const std::exception& e) {
        spdlog::warn("ani.cues: the calendar source failed: {}", e.what());
        reply(callback, in);
        return;
    }
    // Google is asked off the IO thread, each call cut to a few seconds, as GET /now/day does.
    auto respond = std::make_shared<Callback>(std::move(callback));
    const bool queued = Utils::BlockingPool::outbound().try_submit([respond, owner, tz, clock, in, reply]() mutable {
        try {
            Net::Http::CappedTransport quick(Net::Http::transport(), kCalendarCallSeconds);
            auto events = Calendar::events(quick, owner, clock.from, clock.to, tz);
            if (auto* list = std::get_if<std::vector<Calendar::Event>>(&events)) {
                CalendarCache::put(owner, clock.from, clock.to, *list);
                in.meetings = meetings_of(*list, clock);
            } else if (std::get<Calendar::Problem>(events).status == 502) {
                CalendarCache::mark_down(owner);
            }
        } catch (const std::exception& e) {
            spdlog::warn("ani.cues: the calendar source failed: {}", e.what());
        }
        reply(*respond, in);
    });
    if (!queued) {
        reply(*respond, in);
    }
}

void AniCuesController::dismiss(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_CUES_GUARD(req, callback, owner);
    const auto day = day_of(req, callback);
    if (!day) {
        return;
    }
    const std::string date = day->first;
    try {
        CueRepository cues;
        const auto agent = Cues::agent_uuid(id);
        if (!Cues::is_rule_id(id) && !(agent && cues.exists(owner, date, *agent))) {
            callback(ErrorResponse::not_found("cue_not_found"));
            return;
        }
        cues.dismiss(owner, date, id);
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("ani.cues dismiss failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
