/**
 * @file TravelController.cpp
 * @brief The /api/v1/travel handlers: validation, the owner scope, the day
 *        counters from the crossings, a trip with its plan, packing and spends
 *        (docs/superpowers/specs/2026-10-13-travel-design.md).
 */

#include "api/TravelController.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "core/Modules.hpp"
#include "money/Summary.hpp"
#include "repositories/money/SummaryRepository.hpp"
#include "repositories/travel/Counters.hpp"
#include "repositories/travel/CrossingRepository.hpp"
#include "repositories/travel/Errors.hpp"
#include "repositories/travel/RuleRepository.hpp"
#include "repositories/travel/SettingsRepository.hpp"
#include "repositories/travel/TripRepository.hpp"
#include "travel/Days.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::date_field;
using Api::Fields::is_calendar_date;
using Api::Fields::opt_int;
using Api::Fields::text_length;
using Api::Fields::today_utc;
using Repositories::Travel::CrossingRepository;
using Repositories::Travel::RuleRepository;
using Repositories::Travel::SettingsRepository;
using Repositories::Travel::TripRepository;

constexpr int kDaysMax = 800;
constexpr int kLinkMax = 500;

const std::vector<std::string> kWindows = {"rolling_days", "rolling_12_months_in_year", "since_last_entry"};
const std::vector<std::string> kThresholds = {"at_most", "over"};
const std::vector<std::string> kItemKinds = {"flight", "stay", "booking", "note"};

bool is_country(const std::string& s) {
    return s.size() == 2 && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z';
}

bool is_time(const std::string& s) {
    static const std::regex re(R"(^([01][0-9]|2[0-3]):[0-5][0-9]$)");
    return std::regex_match(s, re);
}

bool parse_object(const HttpRequestPtr& req, json& body, TravelController::Callback& callback) {
    if (!Validation::parse_body(req, body, callback)) {
        return false;
    }
    if (!body.is_object()) {
        callback(ErrorResponse::bad_request("invalid_body", "the body must be a JSON object"));
        return false;
    }
    return true;
}

bool require_user(const std::string& owner, const TravelController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

/// ?date=, today in UTC by default; nullopt (answered) when bad.
std::optional<std::string> at_date(const HttpRequestPtr& req, const TravelController::Callback& callback) {
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
    if (!is_calendar_date(date)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day"));
        return std::nullopt;
    }
    return date;
}

void country_field(Validation::Errors& errs, const json& body, const char* field) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string() || !is_country(body[field].get<std::string>())) {
        errs.add(field, "invalid", "must be an ISO 3166-1 alpha-2 code in upper case");
    }
}

void time_field(Validation::Errors& errs, const json& body, const char* field) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string() || !is_time(body[field].get<std::string>())) {
        errs.add(field, "invalid", "must be HH:MM");
    }
}

/// A list of country codes (rules also take the group key SCHENGEN).
std::optional<std::set<std::string>> countries_field(
    Validation::Errors& errs, const json& body, const char* field, bool allow_group, bool required) {
    if (!body.contains(field) || body[field].is_null()) {
        if (required) {
            errs.add(field, "missing", "required");
        }
        return std::nullopt;
    }
    if (!body[field].is_array() || body[field].size() > 60) {
        errs.add(field, "invalid", "must be a list of at most 60 country codes");
        return std::nullopt;
    }
    std::set<std::string> out;
    for (const auto& c : body[field]) {
        const std::string s = c.is_string() ? c.get<std::string>() : std::string();
        if (!is_country(s) && !(allow_group && s == "SCHENGEN")) {
            errs.add(field, "invalid", "every entry must be an ISO 3166-1 alpha-2 code in upper case");
            return std::nullopt;
        }
        out.insert(s);
    }
    if (required && out.empty()) {
        errs.add(field, "missing", "at least one country");
    }
    return out;
}

std::optional<std::string> opt_text(const json& body, const char* field) {
    return Validation::opt_string(body, field);
}

/// A crossing body, checked; nullopt (answered) when bad.
std::optional<CrossingRepository::Input> crossing_input(const json& body, const TravelController::Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "date");
    Validation::require_string(errs, body, "from");
    Validation::require_string(errs, body, "to");
    date_field(errs, body, "date");
    country_field(errs, body, "from");
    country_field(errs, body, "to");
    time_field(errs, body, "time");
    text_length(errs, body, "place", 0, 120);
    text_length(errs, body, "note", 0, 500);
    if (!errs.any() && body["from"] == body["to"]) {
        errs.add("to", "invalid", "a crossing goes to another country");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return std::nullopt;
    }
    return CrossingRepository::Input{body["date"].get<std::string>(),
                                     body["from"].get<std::string>(),
                                     body["to"].get<std::string>(),
                                     opt_text(body, "time"),
                                     opt_text(body, "place").value_or(""),
                                     opt_text(body, "note").value_or("")};
}

std::optional<RuleRepository::Input> rule_input(const json& body, const TravelController::Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::require_string(errs, body, "window_kind");
    Validation::require_string(errs, body, "threshold_kind");
    text_length(errs, body, "name", 1, 80);
    text_length(errs, body, "source", 0, 600);
    Validation::one_of(errs, body, "window_kind", kWindows);
    Validation::one_of(errs, body, "threshold_kind", kThresholds);
    const auto countries = countries_field(errs, body, "countries", true, true);
    const auto threshold = opt_int(body, "threshold_days");
    if (!threshold || *threshold < 1 || *threshold > 3650) {
        errs.add("threshold_days", "invalid", "a whole number of days from 1 to 3650");
    }
    const std::string window = opt_text(body, "window_kind").value_or("");
    const auto window_days = opt_int(body, "window_days");
    if (window == "rolling_days" && (!window_days || *window_days < 1 || *window_days > 3650)) {
        errs.add("window_days", "invalid", "a rolling window needs window_days from 1 to 3650");
    }
    if (window != "rolling_days" && body.contains("window_days") && !body["window_days"].is_null()) {
        errs.add("window_days", "not_allowed", "only a rolling_days window takes window_days");
    }
    if (body.contains("count_crossing_days") && !body["count_crossing_days"].is_boolean()) {
        errs.add("count_crossing_days", "invalid", "must be true or false");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return std::nullopt;
    }
    return RuleRepository::Input{
        body["name"].get<std::string>(),
        *countries,
        window,
        window == "rolling_days" ? window_days : std::nullopt,
        *threshold,
        body["threshold_kind"].get<std::string>(),
        body.value("count_crossing_days", true),
        opt_text(body, "source").value_or(""),
    };
}

std::optional<TripRepository::ItemInput> item_input(const json& body, const TravelController::Callback& callback) {
    Validation::Errors errs;
    Validation::require_string(errs, body, "kind");
    Validation::require_string(errs, body, "date");
    Validation::require_string(errs, body, "title");
    Validation::one_of(errs, body, "kind", kItemKinds);
    date_field(errs, body, "date");
    time_field(errs, body, "time");
    text_length(errs, body, "title", 1, 200);
    text_length(errs, body, "reference", 0, 80);
    text_length(errs, body, "link", 0, 500);
    text_length(errs, body, "note", 0, 1000);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return std::nullopt;
    }
    return TripRepository::ItemInput{body["kind"].get<std::string>(),
                                     body["date"].get<std::string>(),
                                     opt_text(body, "time"),
                                     body["title"].get<std::string>(),
                                     opt_text(body, "reference").value_or(""),
                                     opt_text(body, "link").value_or(""),
                                     opt_text(body, "note").value_or("")};
}

/// The trip's spends in each currency and in @p target at each spend's day rate.
json spends_summary(const json& spends, const std::string& target) {
    std::map<std::string, double> by_currency;
    std::set<std::string> currencies{target};
    std::string first;
    std::string last;
    for (const auto& s : spends) {
        const std::string c = s["currency"].get<std::string>();
        by_currency[c] += s["amount"].get<double>();
        currencies.insert(c);
        const std::string d = s["date"].get<std::string>();
        first = first.empty() ? d : std::min(first, d);
        last = std::max(last, d);
    }
    json list = json::array();
    for (const auto& [c, amount] : by_currency) {
        list.push_back({{"currency", c}, {"amount", amount}});
    }
    json total = {{"currency", target}, {"amount", 0.0}, {"partial", false}, {"rate_date", nullptr}};
    if (spends.empty()) {
        return {{"by_currency", list}, {"total", total}, {"count", 0}};
    }
    const auto quotes = Repositories::Money::SummaryRepository().quotes(currencies, first, last);
    double sum = 0;
    bool partial = false;
    std::string rate_date;
    for (const auto& s : spends) {
        const std::string c = s["currency"].get<std::string>();
        const std::string d = s["date"].get<std::string>();
        const auto rate = ::Money::Summary::rate_per_target(quotes, c, target, d);
        if (!rate || *rate <= 0) {
            partial = true;
            continue;
        }
        sum += s["amount"].get<double>() / *rate;
        const auto from = ::Money::Summary::nearest(quotes, c, d);
        const auto to = ::Money::Summary::nearest(quotes, target, d);
        const std::string used = std::min(from->date, to->date);
        rate_date = rate_date.empty() ? used : std::min(rate_date, used);
    }
    total["amount"] = sum;
    total["partial"] = partial;
    total["rate_date"] = rate_date.empty() ? json() : json(rate_date);
    return {{"by_currency", list}, {"total", total}, {"count", spends.size()}};
}

/// The target currency of a trip summary: ?currency=, USD by default; nullopt (answered) when bad.
std::optional<std::string> target_currency(const HttpRequestPtr& req, const TravelController::Callback& callback) {
    const std::string c = req->getParameter("currency").empty() ? "USD" : req->getParameter("currency");
    static const std::regex re("^[A-Z]{3,10}$");
    if (!std::regex_match(c, re)) {
        callback(ErrorResponse::bad_request("invalid_currency", "currency must be a currency code"));
        return std::nullopt;
    }
    return c;
}

json trip_view(TripRepository& repo, const std::string& owner, json row, const std::string& currency, bool full) {
    const std::string id = row["id"].get<std::string>();
    const auto [done, all] = repo.packing(owner, id);
    row["packing"] = {{"done", done}, {"total", all}};
    const auto spends = repo.spends(owner, id);
    row["spends"] = spends_summary(spends, currency);
    if (full) {
        row["items"] = repo.items(owner, id);
        row["spend_rows"] = spends;
    }
    return row;
}

}  // namespace

#define TRAVEL_GUARD(req, callback, owner)   \
    if (!require_enabled(callback))          \
        return;                              \
    API_REQUIRE_OWNER(req, callback, owner); \
    if (!require_user(owner, callback))      \
    return

bool TravelController::require_enabled(const Callback& callback) {
    if (Core::travel_enabled()) {
        return true;
    }
    callback(ErrorResponse::not_found("travel"));
    return false;
}

namespace {

void no_content(const TravelController::Callback& callback) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k204NoContent);
    callback(resp);
}

}  // namespace

// ── settings ────────────────────────────────────────────────────────────────

void TravelController::getSettings(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    with_repo_errors(callback, "travel.settings", [&] {
        const auto home = SettingsRepository().home(owner);
        callback(Response::ok(json{{"data", {{"home_country", home ? json(*home) : json()}}}}));
    });
}

void TravelController::putSettings(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "home_country");
    country_field(errs, body, "home_country");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "travel.settings.put", [&] {
        SettingsRepository().put_home(owner, body["home_country"].get<std::string>());
        callback(Response::ok(json{{"data", {{"home_country", body["home_country"]}}}}));
    });
}

// ── crossings ───────────────────────────────────────────────────────────────

void TravelController::listCrossings(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    const auto page = parse_page_params(req, 100, 500);
    with_repo_errors(callback, "travel.crossings", [&] {
        CrossingRepository repo;
        callback(Response::ok(json{{"data", repo.list(owner, page.limit, page.offset)},
                                   {"total", repo.total(owner)},
                                   {"limit", page.limit},
                                   {"offset", page.offset}}));
    });
}

void TravelController::createCrossing(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = crossing_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "travel.crossings.create", [&] {
        callback(Response::created(json{{"data", CrossingRepository().create(owner, *in)}}));
    });
}

void TravelController::updateCrossing(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = crossing_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "travel.crossings.update", [&] {
        callback(Response::ok(json{{"data", CrossingRepository().replace(owner, id, *in)}}));
    });
}

void TravelController::deleteCrossing(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "travel.crossings.delete", [&] {
        CrossingRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── rules ───────────────────────────────────────────────────────────────────

void TravelController::listRules(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    with_repo_errors(callback, "travel.rules", [&] {
        callback(Response::ok(json{{"data", RuleRepository().list(owner)}}));
    });
}

void TravelController::createRule(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = rule_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "travel.rules.create", [&] {
        callback(Response::created(json{{"data", RuleRepository().create(owner, *in)}}));
    });
}

void TravelController::addTemplate(const HttpRequestPtr& req, Callback&& callback, const std::string& key) {
    TRAVEL_GUARD(req, callback, owner);
    const auto in = RuleRepository::template_of(key);
    if (!in) {
        callback(ErrorResponse::not_found("template"));
        return;
    }
    with_repo_errors(callback, "travel.rules.template", [&] {
        callback(Response::created(json{{"data", RuleRepository().create(owner, *in)}}));
    });
}

void TravelController::updateRule(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = rule_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "travel.rules.update", [&] {
        callback(Response::ok(json{{"data", RuleRepository().replace(owner, id, *in)}}));
    });
}

void TravelController::deleteRule(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "travel.rules.delete", [&] {
        RuleRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── counters and days ───────────────────────────────────────────────────────

void TravelController::counters(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    const auto date = at_date(req, callback);
    if (!date) {
        return;
    }
    with_repo_errors(callback, "travel.counters", [&] {
        const auto crossings = CrossingRepository().all(owner);
        const std::string home = Repositories::Travel::home_of(owner, crossings);
        callback(Response::ok(json{
            {"data", Repositories::Travel::counters_json(owner, *date)},
            {"date", *date},
            {"current_country",
             home.empty() && crossings.empty()
                 ? json()
                 : json(::Travel::days_by_country(crossings, home, ::Travel::day_of(*date), ::Travel::day_of(*date))
                            .begin()
                            ->second)}}));
    });
}

void TravelController::days(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    if (!is_calendar_date(from) || !is_calendar_date(to) || to < from) {
        callback(ErrorResponse::bad_request("invalid_query", "from and to must be calendar days, from first"));
        return;
    }
    const auto a = ::Travel::day_of(from);
    const auto b = ::Travel::day_of(to);
    if ((b - a).count() >= kDaysMax) {
        callback(ErrorResponse::bad_request("invalid_query", "at most 800 days at a time"));
        return;
    }
    with_repo_errors(callback, "travel.days", [&] {
        const auto crossings = CrossingRepository().all(owner);
        const std::string home = Repositories::Travel::home_of(owner, crossings);
        json out = json::array();
        for (const auto& [day, countries] : ::Travel::days_by_country(crossings, home, a, b)) {
            json list = json::array();
            for (const auto& c : countries) {
                if (!c.empty()) {
                    list.push_back(c);
                }
            }
            out.push_back({{"date", ::Travel::text_of(day)}, {"countries", list}});
        }
        callback(Response::ok(json{{"data", out}}));
    });
}

// ── trips ───────────────────────────────────────────────────────────────────

void TravelController::listTrips(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    const auto currency = target_currency(req, callback);
    if (!currency) {
        return;
    }
    with_repo_errors(callback, "travel.trips", [&] {
        TripRepository repo;
        json rows = json::array();
        for (const auto& row : repo.list(owner)) {
            rows.push_back(trip_view(repo, owner, row, *currency, false));
        }
        callback(Response::ok(json{{"data", rows}}));
    });
}

void TravelController::createTrip(const HttpRequestPtr& req, Callback&& callback) {
    TRAVEL_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::require_string(errs, body, "start_date");
    Validation::require_string(errs, body, "end_date");
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "note", 0, 2000);
    date_field(errs, body, "start_date");
    date_field(errs, body, "end_date");
    const auto countries = countries_field(errs, body, "countries", false, false);
    if (!errs.any() && body["end_date"].get<std::string>() < body["start_date"].get<std::string>()) {
        errs.add("end_date", "invalid", "a trip ends on or after its start");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "travel.trips.create", [&] {
        TripRepository repo;
        const auto row = repo.create(owner,
                                     {body["name"].get<std::string>(),
                                      body["start_date"].get<std::string>(),
                                      body["end_date"].get<std::string>(),
                                      countries.value_or(std::set<std::string>{}),
                                      opt_text(body, "note").value_or("")});
        callback(Response::created(json{{"data", trip_view(repo, owner, row, "USD", true)}}));
    });
}

void TravelController::getTrip(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    const auto currency = target_currency(req, callback);
    if (!currency) {
        return;
    }
    with_repo_errors(callback, "travel.trips.get", [&] {
        TripRepository repo;
        const auto row = repo.get(owner, id);
        if (!row) {
            throw Repositories::Travel::NotFound("trip");
        }
        callback(Response::ok(json{{"data", trip_view(repo, owner, *row, *currency, true)}}));
    });
}

void TravelController::updateTrip(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "note", 0, 2000);
    date_field(errs, body, "start_date");
    date_field(errs, body, "end_date");
    const auto countries = countries_field(errs, body, "countries", false, false);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    TripRepository::Patch p;
    p.name = opt_text(body, "name");
    p.start_date = opt_text(body, "start_date");
    p.end_date = opt_text(body, "end_date");
    p.countries = countries;
    p.note = opt_text(body, "note");
    with_repo_errors(callback, "travel.trips.update", [&] {
        TripRepository repo;
        callback(Response::ok(json{{"data", trip_view(repo, owner, repo.update(owner, id, p), "USD", true)}}));
    });
}

void TravelController::deleteTrip(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "travel.trips.delete", [&] {
        TripRepository().remove(owner, id);
        no_content(callback);
    });
}

void TravelController::addItem(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = item_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "travel.items.create", [&] {
        callback(Response::created(json{{"data", TripRepository().add_item(owner, id, *in)}}));
    });
}

void TravelController::updateItem(const HttpRequestPtr& req,
                                  Callback&& callback,
                                  const std::string& id,
                                  const std::string& child) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto in = item_input(body, callback);
    if (!in) {
        return;
    }
    with_repo_errors(callback, "travel.items.update", [&] {
        callback(Response::ok(json{{"data", TripRepository().replace_item(owner, id, child, *in)}}));
    });
}

void TravelController::deleteItem(const HttpRequestPtr& req,
                                  Callback&& callback,
                                  const std::string& id,
                                  const std::string& child) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback) || !require_valid_uuid(child, callback)) {
        return;
    }
    with_repo_errors(callback, "travel.items.delete", [&] {
        TripRepository().remove_item(owner, id, child);
        no_content(callback);
    });
}

void TravelController::candidateSpends(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "travel.spends.candidates", [&] {
        TripRepository repo;
        if (!repo.get(owner, id)) {
            throw Repositories::Travel::NotFound("trip");
        }
        callback(Response::ok(json{{"data", repo.candidate_spends(owner, id)}}));
    });
}

void TravelController::linkSpends(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    TRAVEL_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    std::vector<std::string> link;
    std::vector<std::string> unlink;
    for (const auto* field : {"link", "unlink"}) {
        if (!body.contains(field) || body[field].is_null()) {
            continue;
        }
        if (!body[field].is_array() || body[field].size() > kLinkMax) {
            errs.add(field, "invalid", "must be a list of at most 500 transaction ids");
            continue;
        }
        for (const auto& v : body[field]) {
            if (!v.is_string() || !is_valid_uuid(v.get<std::string>())) {
                errs.add(field, "invalid", "every entry must be a transaction id");
                break;
            }
            (std::string(field) == "link" ? link : unlink).push_back(v.get<std::string>());
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "travel.spends.link", [&] {
        TripRepository repo;
        int changed = 0;
        if (!link.empty()) {
            changed += repo.link_spends(owner, id, link, true);
        }
        if (!unlink.empty()) {
            changed += repo.link_spends(owner, id, unlink, false);
        }
        if (link.empty() && unlink.empty() && !repo.get(owner, id)) {
            throw Repositories::Travel::NotFound("trip");
        }
        callback(Response::ok(json{{"data", {{"changed", changed}}}}));
    });
}

}  // namespace Api
