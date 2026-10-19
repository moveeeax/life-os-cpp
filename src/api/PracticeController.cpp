/**
 * @file PracticeController.cpp
 * @brief The practice module: clients, deals and the incomes and expenses of
 *        the practice. Every link is the caller's own; an amount is positive,
 *        in its own currency and in its major units (1500.50 RUB), with no
 *        more decimals than the currency has; the months answer totals per
 *        currency and never add two currencies together.
 */

#include "api/PracticeController.hpp"

#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "money/Currencies.hpp"
#include "practice/Months.hpp"
#include "repositories/practice/ClientRepository.hpp"
#include "repositories/practice/DealRepository.hpp"
#include "repositories/practice/EntryRepository.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Api::Fields::amount_range;
using Api::Fields::date_field;
using Api::Fields::opt_number;
using Api::Fields::text_length;
using Repositories::Practice::ClientRepository;
using Repositories::Practice::DealRepository;
using Repositories::Practice::EntryRepository;

constexpr double kAmountMax = 1e12;
/// One unit of the eighth decimal, the finest the numeric(24,8) columns keep.
constexpr double kAmountMin = 0.00000001;
const std::regex kCodeRe("^[A-Z]{3}$");
const std::vector<std::string> kKinds = {"income", "expense"};

std::vector<std::string> stages() {
    return {::Practice::kStages.begin(), ::Practice::kStages.end()};
}

void code_field(Validation::Errors& errs, const json& body, const std::string& field) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string() || !std::regex_match(body[field].get<std::string>(), kCodeRe)) {
        errs.add(field, "bad_format", "expected an ISO code like USD");
    }
}

/// An amount fits its currency: a known code has its own decimals, another code up to eight.
void amount_fits(Validation::Errors& errs, const char* field, double amount, const std::string& currency) {
    const auto iso = ::Money::Currencies::known(currency);
    const int decimals = iso ? iso->decimals : 8;
    if (!::Money::Currencies::fits(amount, decimals)) {
        errs.add(field, "precision", currency + " has " + std::to_string(decimals) + " decimal(s)");
    }
}

void uuid_field(Validation::Errors& errs, const json& body, const char* field) {
    if (body.contains(field) && !body[field].is_null()) {
        Validation::uuid(errs, body, field);
    }
}

std::optional<std::optional<std::string>> nullable(const json& body, const char* field) {
    if (!body.contains(field)) {
        return std::nullopt;
    }
    return std::optional<std::optional<std::string>>(std::in_place, Validation::opt_string(body, field));
}

bool given(const json& body, const char* field) {
    return body.contains(field) && !body[field].is_null();
}

// ── clients ─────────────────────────────────────────────────────────────────

void client_fields(Validation::Errors& errs, const json& body, bool create) {
    if (create) {
        Validation::require_string(errs, body, "name");
    }
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "contact", 0, 300);
    text_length(errs, body, "note", 0, 2000);
}

// ── deals ───────────────────────────────────────────────────────────────────

void deal_fields(Validation::Errors& errs, const json& body, const std::string& owner, bool create) {
    if (create) {
        Validation::require_string(errs, body, "client_id");
        Validation::require_string(errs, body, "title");
    }
    if (body.contains("client_id") && body["client_id"].is_null()) {
        errs.add("client_id", "missing", "a deal has a client");
    }
    uuid_field(errs, body, "client_id");
    text_length(errs, body, "title", 1, 200);
    if (body.contains("stage")) {
        Validation::require_string(errs, body, "stage");
        Validation::one_of(errs, body, "stage", stages());
    }
    amount_range(errs, body, "value", kAmountMin, kAmountMax);
    code_field(errs, body, "currency");
    // A value is an amount and a currency: both given, both null, or neither.
    if (body.contains("value") != body.contains("currency") || given(body, "value") != given(body, "currency")) {
        errs.add("currency", "invariant", "a value is an amount and a currency together");
    }
    date_field(errs, body, "start_date");
    date_field(errs, body, "end_date");
    text_length(errs, body, "note", 0, 2000);
    if (errs.any()) {
        return;
    }
    if (given(body, "value")) {
        amount_fits(errs, "value", body["value"].get<double>(), body["currency"].get<std::string>());
    }
    if (given(body, "start_date") && given(body, "end_date") &&
        body["end_date"].get<std::string>() < body["start_date"].get<std::string>()) {
        errs.add("end_date", "before_start", "the end is on or after the start");
    }
    if (given(body, "client_id") && !ClientRepository().owns(owner, body["client_id"].get<std::string>())) {
        errs.add("client_id", "not_yours", "the client is not yours");
    }
}

// ── entries ─────────────────────────────────────────────────────────────────

void entry_fields(Validation::Errors& errs, const json& body, const std::string& owner, bool create) {
    if (create) {
        Validation::require_string(errs, body, "kind");
        Validation::require_string(errs, body, "date");
        Validation::require(errs, body, "amount");
        Validation::require_string(errs, body, "currency");
    }
    for (const char* f : {"kind", "date", "amount", "currency"}) {
        if (body.contains(f) && body[f].is_null()) {
            errs.add(f, "missing", "cannot be null");
        }
    }
    Validation::one_of(errs, body, "kind", kKinds);
    date_field(errs, body, "date");
    amount_range(errs, body, "amount", kAmountMin, kAmountMax);
    code_field(errs, body, "currency");
    uuid_field(errs, body, "client_id");
    uuid_field(errs, body, "deal_id");
    text_length(errs, body, "note", 0, 500);
    if (errs.any()) {
        return;
    }
    if (given(body, "client_id") && !ClientRepository().owns(owner, body["client_id"].get<std::string>())) {
        errs.add("client_id", "not_yours", "the client is not yours");
    }
    if (given(body, "deal_id") && !DealRepository().client_of(owner, body["deal_id"].get<std::string>())) {
        errs.add("deal_id", "not_yours", "the deal is not yours");
    }
}

/// The month of a query or a path: a YYYY-MM of 1900..2100.
std::optional<::Practice::Months::Range> month_of(const std::string& text) {
    return ::Practice::Months::parse(text);
}

}  // namespace

// ── clients ─────────────────────────────────────────────────────────────────

void PracticeController::listClients(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    with_repo_errors(callback, "practice.listClients", [&] {
        callback(Response::ok(json{{"data", ClientRepository().list(owner)}}));
    });
}

void PracticeController::createClient(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    client_fields(errs, body, true);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "practice.createClient", [&] {
        ClientRepository::Input in;
        in.name = body["name"].get<std::string>();
        in.contact = Validation::opt_string(body, "contact").value_or("");
        in.note = Validation::opt_string(body, "note").value_or("");
        callback(Response::created(json{{"data", ClientRepository().create(owner, in)}}));
    });
}

void PracticeController::getClient(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "practice.getClient", [&] {
        const auto row = ClientRepository().get(owner, id);
        if (!row) {
            throw Repositories::Practice::NotFound("client");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void PracticeController::updateClient(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    client_fields(errs, body, false);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "practice.updateClient", [&] {
        ClientRepository::Patch p;
        p.name = Validation::opt_string(body, "name");
        p.contact = Validation::opt_string(body, "contact");
        p.note = Validation::opt_string(body, "note");
        callback(Response::ok(json{{"data", ClientRepository().update(owner, id, p)}}));
    });
}

void PracticeController::deleteClient(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "practice.deleteClient", [&] {
        ClientRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── deals ───────────────────────────────────────────────────────────────────

void PracticeController::listDeals(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    DealRepository::Filter f;
    f.stage = req->getParameter("stage");
    f.client = req->getParameter("client");
    if ((!f.stage.empty() && !::Practice::is_stage(f.stage)) || (!f.client.empty() && !is_valid_uuid(f.client))) {
        callback(ErrorResponse::bad_request("invalid_query", "stage must be a known stage and client an id"));
        return;
    }
    with_repo_errors(callback, "practice.listDeals", [&] {
        callback(Response::ok(json{{"data", DealRepository().list(owner, f)}}));
    });
}

void PracticeController::createDeal(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    deal_fields(errs, body, owner, true);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "practice.createDeal", [&] {
        DealRepository::Input in;
        in.client_id = body["client_id"].get<std::string>();
        in.title = body["title"].get<std::string>();
        in.stage = Validation::opt_string(body, "stage").value_or("lead");
        in.value = opt_number(body, "value");
        in.currency = Validation::opt_string(body, "currency");
        in.start_date = Validation::opt_string(body, "start_date");
        in.end_date = Validation::opt_string(body, "end_date");
        in.note = Validation::opt_string(body, "note").value_or("");
        callback(Response::created(json{{"data", DealRepository().create(owner, in)}}));
    });
}

void PracticeController::getDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "practice.getDeal", [&] {
        const auto row = DealRepository().get(owner, id);
        if (!row) {
            throw Repositories::Practice::NotFound("deal");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void PracticeController::updateDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    deal_fields(errs, body, owner, false);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "practice.updateDeal", [&] {
        DealRepository::Patch p;
        p.client_id = Validation::opt_string(body, "client_id");
        p.title = Validation::opt_string(body, "title");
        p.stage = Validation::opt_string(body, "stage");
        if (body.contains("value")) {
            p.value = std::optional<std::optional<double>>(std::in_place, opt_number(body, "value"));
        }
        p.currency = nullable(body, "currency");
        p.start_date = nullable(body, "start_date");
        p.end_date = nullable(body, "end_date");
        p.note = Validation::opt_string(body, "note");
        callback(Response::ok(json{{"data", DealRepository().update(owner, id, p)}}));
    });
}

void PracticeController::deleteDeal(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "practice.deleteDeal", [&] {
        DealRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── entries ─────────────────────────────────────────────────────────────────

void PracticeController::listEntries(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    EntryRepository::Filter f;
    f.kind = req->getParameter("kind");
    f.client = req->getParameter("client");
    f.deal = req->getParameter("deal");
    f.limit = clamp_int(req->getParameter("limit"), kDefaultLimit, 1, kMaxLimit);
    f.offset = clamp_int(req->getParameter("offset"), 0, 0, 1000000);
    const std::string month = req->getParameter("month");
    const auto range = month.empty() ? std::nullopt : month_of(month);
    if ((!f.kind.empty() && f.kind != "income" && f.kind != "expense") || (!month.empty() && !range) ||
        (!f.client.empty() && !is_valid_uuid(f.client)) || (!f.deal.empty() && !is_valid_uuid(f.deal))) {
        callback(ErrorResponse::bad_request("invalid_query",
                                            "kind must be income or expense, month a YYYY-MM, client and deal ids"));
        return;
    }
    if (range) {
        f.from = range->first;
        f.before = range->next;
    }
    with_repo_errors(callback, "practice.listEntries", [&] {
        const json page = EntryRepository().list(owner, f);
        callback(Response::ok(
            json{{"data", page["data"]}, {"total", page["total"]}, {"limit", f.limit}, {"offset", f.offset}}));
    });
}

void PracticeController::createEntry(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    entry_fields(errs, body, owner, true);
    if (!errs.any()) {
        amount_fits(errs, "amount", body["amount"].get<double>(), body["currency"].get<std::string>());
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "practice.createEntry", [&] {
        EntryRepository::Input in;
        in.kind = body["kind"].get<std::string>();
        in.date = body["date"].get<std::string>();
        in.amount = body["amount"].get<double>();
        in.currency = body["currency"].get<std::string>();
        in.client_id = Validation::opt_string(body, "client_id");
        in.deal_id = Validation::opt_string(body, "deal_id");
        in.note = Validation::opt_string(body, "note").value_or("");
        callback(Response::created(json{{"data", EntryRepository().create(owner, in)}}));
    });
}

void PracticeController::getEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "practice.getEntry", [&] {
        const auto row = EntryRepository().get(owner, id);
        if (!row) {
            throw Repositories::Practice::NotFound("entry");
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void PracticeController::updateEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    entry_fields(errs, body, owner, false);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "practice.updateEntry", [&] {
        EntryRepository repo;
        if (given(body, "amount") || given(body, "currency")) {
            // The amount is checked against the currency it ends up in.
            const auto row = repo.get(owner, id);
            if (!row) {
                throw Repositories::Practice::NotFound("entry");
            }
            const double amount = given(body, "amount") ? body["amount"].get<double>() : (*row)["amount"].get<double>();
            const std::string currency =
                Validation::opt_string(body, "currency").value_or((*row)["currency"].get<std::string>());
            amount_fits(errs, "amount", amount, currency);
            if (errs.any()) {
                callback(Validation::response_400(errs));
                return;
            }
        }
        EntryRepository::Patch p;
        p.kind = Validation::opt_string(body, "kind");
        p.date = Validation::opt_string(body, "date");
        p.amount = opt_number(body, "amount");
        p.currency = Validation::opt_string(body, "currency");
        p.client_id = nullable(body, "client_id");
        p.deal_id = nullable(body, "deal_id");
        p.note = Validation::opt_string(body, "note");
        callback(Response::ok(json{{"data", repo.update(owner, id, p)}}));
    });
}

void PracticeController::deleteEntry(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "practice.deleteEntry", [&] {
        EntryRepository().remove(owner, id);
        no_content(callback);
    });
}

// ── months ──────────────────────────────────────────────────────────────────

void PracticeController::listMonths(const HttpRequestPtr& req, Callback&& callback) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    const std::string text = req->getParameter("year");
    const auto year = text.empty() ? ::Practice::Months::year_of(Api::Fields::today_utc().substr(0, 4))
                                   : ::Practice::Months::year_of(text);
    if (!year) {
        callback(ErrorResponse::bad_request("invalid_query", "year must be a year like 2026"));
        return;
    }
    const auto range = ::Practice::Months::year_range(*year);
    with_repo_errors(callback, "practice.listMonths", [&] {
        json out = EntryRepository().months(owner, range.first, range.next);
        out["year"] = *year;
        callback(Response::ok(out));
    });
}

void PracticeController::getMonth(const HttpRequestPtr& req, Callback&& callback, const std::string& month) {
    API_MODULE_USER_GUARD(Core::practice_enabled(), "practice", req, callback, owner);
    const auto range = month_of(month);
    if (!range) {
        callback(ErrorResponse::bad_request("invalid_month", "the month is a YYYY-MM"));
        return;
    }
    with_repo_errors(callback, "practice.getMonth", [&] {
        json data = EntryRepository().month(owner, range->first, range->next);
        data["month"] = month;
        callback(Response::ok(json{{"data", data}}));
    });
}

}  // namespace Api
