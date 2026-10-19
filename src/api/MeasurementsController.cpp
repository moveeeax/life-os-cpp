/**
 * @file MeasurementsController.cpp
 * @brief Bodies for src/api/MeasurementsController.hpp: the checks of a
 *        measurement (a calendar day, girths from 0 to 400 cm, at least one of
 *        them, a short note) and the owner scope on every id route. No
 *        permission bit: the rows are the caller's own, like the medical
 *        archive.
 */

#include "api/MeasurementsController.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "repositories/fitness/TapeRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::date_field;
using Api::Fields::is_calendar_date;
using Api::Fields::text_length;
using Repositories::Tape::TapeRepository;

constexpr double kGirthMax = 400;
constexpr std::size_t kNoteMax = 500;

/// Each girth field: a number above 0 and below 400, or null.
void girth_fields(Validation::Errors& errs, const json& body) {
    for (const auto name : TapeRepository::kGirths) {
        const std::string field(name);
        if (!body.contains(field) || body[field].is_null()) {
            continue;
        }
        if (!body[field].is_number() || !(body[field].get<double>() > 0) || !(body[field].get<double>() < kGirthMax)) {
            errs.add(field, "invalid", "a girth in cm: a number above 0 and below 400");
        }
    }
}

std::optional<double> girth_of(const json& body, std::string_view name) {
    const std::string field(name);
    if (body.contains(field) && body[field].is_number()) {
        return body[field].get<double>();
    }
    return std::nullopt;
}

}  // namespace

void MeasurementsController::list(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::fitness_enabled(), "fitness", req, callback, owner);
    TapeRepository::Filter f;
    f.from = req->getParameter("from");
    f.to = req->getParameter("to");
    if ((!f.from.empty() && !is_calendar_date(f.from)) || (!f.to.empty() && !is_calendar_date(f.to)) ||
        (!f.from.empty() && !f.to.empty() && f.to < f.from)) {
        callback(ErrorResponse::bad_request("invalid_query", "from and to must be calendar days, from first"));
        return;
    }
    f.limit = clamp_int(req->getParameter("limit"), kDefaultLimit, 1, kMaxLimit);
    f.offset = clamp_int(req->getParameter("offset"), 0, 0, 1000000);
    with_repo_errors(callback, "measurements.list", [&] { callback(Response::ok(TapeRepository().list(owner, f))); });
}

void MeasurementsController::create(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::fitness_enabled(), "fitness", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "date");
    date_field(errs, body, "date");
    girth_fields(errs, body);
    text_length(errs, body, "note", 0, kNoteMax);
    TapeRepository::Input in;
    bool any = false;
    for (std::size_t i = 0; i < TapeRepository::kGirths.size(); ++i) {
        in.girths[i] = girth_of(body, TapeRepository::kGirths[i]);
        any = any || in.girths[i].has_value();
    }
    if (!any && !errs.any()) {
        errs.add("waist_cm", "missing", "give at least one girth");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    in.date = body["date"].get<std::string>();
    in.note = Validation::opt_string(body, "note").value_or("");
    with_repo_errors(callback, "measurements.create", [&] {
        callback(Response::created(json{{"data", TapeRepository().create(owner, in)}}));
    });
}

void MeasurementsController::update(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::fitness_enabled(), "fitness", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    if (body.contains("date") && body["date"].is_null()) {
        errs.add("date", "invalid", "a measurement keeps its date");
    }
    date_field(errs, body, "date");
    girth_fields(errs, body);
    text_length(errs, body, "note", 0, kNoteMax);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    TapeRepository::Patch p;
    p.date = Validation::opt_string(body, "date");
    p.note = Validation::opt_string(body, "note");
    for (std::size_t i = 0; i < TapeRepository::kGirths.size(); ++i) {
        const std::string field(TapeRepository::kGirths[i]);
        if (body.contains(field)) {
            p.girths[i].emplace(girth_of(body, field));
        }
    }
    with_repo_errors(callback, "measurements.update", [&] {
        callback(Response::ok(json{{"data", TapeRepository().update(owner, id, p)}}));
    });
}

void MeasurementsController::remove(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::fitness_enabled(), "fitness", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "measurements.delete", [&] {
        TapeRepository().remove(owner, id);
        no_content(callback);
    });
}

}  // namespace Api
