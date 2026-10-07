/**
 * @file MoneyController.cpp
 * @brief Handlers of the /api/v1/money routes. Validation names the field and mirrors
 *        the table CHECKs; the repositories' Invariant is the second line
 *        of defence and answers 400 too.
 */

#include "api/MoneyController.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <pqxx/pqxx>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/FieldChecks.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "core/Modules.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/MoneyAdvisorHandler.hpp"
#include "jobs/MoneyParseHandler.hpp"
#include "jobs/MoneyRatesHandler.hpp"
#include "money/Currencies.hpp"
#include "money/Llm.hpp"
#include "money/Period.hpp"
#include "money/Rates.hpp"
#include "money/Reports.hpp"
#include "repositories/money/AccountRepository.hpp"
#include "repositories/money/AdvisorReportRepository.hpp"
#include "repositories/money/CategoryRepository.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "repositories/money/Errors.hpp"
#include "repositories/money/FxRateRepository.hpp"
#include "repositories/money/MerchantRepository.hpp"
#include "repositories/money/ParseJobRepository.hpp"
#include "repositories/money/ReportBuilder.hpp"
#include "repositories/money/SettingsRepository.hpp"
#include "repositories/money/TransactionRepository.hpp"
#include "repositories/money/TransferRepository.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
namespace Repo = Repositories::Money;

using Api::Fields::date_field;
using Api::Fields::is_calendar_date;
using Api::Fields::number_range;
using Api::Fields::opt_int;
using Api::Fields::opt_number;
using Api::Fields::text_length;
using Api::Fields::today_utc;

constexpr int kDefaultLimit = 50;
constexpr int kMaxLimit = 200;
constexpr std::size_t kBatchMax = 100;
constexpr double kAmountMax = 1e12;
constexpr std::size_t kNameMax = 200;
constexpr std::size_t kNoteMax = 2000;
constexpr std::size_t kParseTextMax = 8000;
constexpr long kParseOpenMax = 3;
constexpr std::size_t kImageMaxBytes = 4 * 1024 * 1024;
// The first day the rates source has a snapshot for (checked 2026-10-07).
constexpr const char* kRatesHistoryStart = "2024-03-02";

const std::vector<std::string> kTypes = {"income", "expense", "fx_adjustment"};
const std::vector<std::string> kKinds = {"expense", "income"};
const std::vector<std::string> kFlex = {"fixed", "variable"};
const std::vector<std::string> kAccountKinds = {"card", "cash", "deposit", "other"};
const std::vector<std::string> kRoles = {"primary", "local"};
const std::vector<std::string> kSources = {"manual", "text", "receipt", "api", "import"};
const std::vector<std::string> kStatuses = {"posted", "pending"};
const std::vector<std::string> kPeriodKinds = {"week", "month", "quarter", "custom"};
const std::regex kCodeRe(R"(^[A-Z]{3}$)");
const std::regex kLast4Re(R"(^[0-9]{0,4}$)");

/// The owner's ten currencies for a user who has none yet. A failure here is not
/// the request's answer: the write that follows reports what is missing.
void seed_currencies(const std::string& owner) {
    try {
        Repo::CurrencyRepository().seed_defaults(owner);
    } catch (const std::exception& e) {
        spdlog::warn("money: seeding currencies failed: {}", e.what());
    }
}

/// The JSON body, which must be an object: an array or a scalar answers 400, not 500.
bool parse_object(const HttpRequestPtr& req, json& body, MoneyController::Callback& callback) {
    if (!Validation::parse_body(req, body, callback)) {
        return false;
    }
    if (!body.is_object()) {
        callback(ErrorResponse::bad_request("invalid_body", "the body must be a JSON object"));
        return false;
    }
    return true;
}

/// Rows belong to an app user; a static-bearer principal has no user id.
bool require_user(const std::string& owner, const MoneyController::Callback& callback) {
    if (is_valid_uuid(owner)) {
        return true;
    }
    callback(ErrorResponse::forbidden("no_user_account", "this route needs a user account"));
    return false;
}

std::optional<std::string> query_param(const HttpRequestPtr& req, const char* name) {
    const std::string v = req->getParameter(name);
    if (v.empty()) {
        return std::nullopt;
    }
    return v;
}

void code_field(Validation::Errors& errs, const json& body, const std::string& field) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string() || !std::regex_match(body[field].get<std::string>(), kCodeRe)) {
        errs.add(field, "bad_format", "expected an ISO code like KZT");
    }
}

void uuid_field(Validation::Errors& errs, const json& body, const std::string& field) {
    if (body.contains(field) && !body[field].is_null()) {
        Validation::uuid(errs, body, field);
    }
}

/// Reads the owner's settings for the "as if" currency the pages remember.
std::optional<std::string> view_currency(const std::string& owner) {
    const json s = Repo::SettingsRepository().load(owner);
    if (s["view_currency"].is_string()) {
        return s["view_currency"].get<std::string>();
    }
    return std::nullopt;
}

// ── transactions ────────────────────────────────────────────────────────────

/// The body as a repository Input; false with the errors recorded.
bool transaction_input(const json& body,
                       const std::string& prefix,
                       Validation::Errors& errs,
                       Repo::TransactionRepository::Input& out) {
    const auto at = [&](const char* f) { return prefix + f; };
    if (!body.is_object()) {
        errs.add(prefix.empty() ? "body" : prefix, "invalid", "must be an object");
        return false;
    }
    Validation::Errors local;
    if (body.contains("type")) {
        Validation::one_of(local, body, "type", kTypes);
    }
    Validation::require_string(local, body, "date");
    date_field(local, body, "date");
    if (body.contains("time") && !body["time"].is_null()) {
        Validation::regex_match(local, body, "time", std::regex(R"(^([01]\d|2[0-3]):[0-5]\d$)"), "expected HH:MM");
    }
    Validation::require_string(local, body, "account_id");
    Validation::uuid(local, body, "account_id");
    Validation::require(local, body, "amount");
    number_range(local, body, "amount", -kAmountMax, kAmountMax);
    uuid_field(local, body, "category_id");
    uuid_field(local, body, "adjusts_id");
    text_length(local, body, "merchant", 0, kNameMax);
    Validation::require_string(local, body, "name");
    text_length(local, body, "name", 1, kNameMax);
    number_range(local, body, "receipt_amount", 0.0001, kAmountMax);
    code_field(local, body, "receipt_currency");
    if ((body.contains("receipt_amount") && !body["receipt_amount"].is_null()) !=
        (body.contains("receipt_currency") && !body["receipt_currency"].is_null())) {
        local.add("receipt_currency", "invariant", "the receipt's amount and currency go together");
    }
    text_length(local, body, "fx_note", 0, 200);
    text_length(local, body, "trip", 0, 60);
    text_length(local, body, "note", 0, kNoteMax);
    if (body.contains("source")) {
        Validation::one_of(local, body, "source", kSources);
    }
    if (body.contains("status")) {
        Validation::one_of(local, body, "status", kStatuses);
    }
    text_length(local, body, "external_id", 1, 120);
    if (local.any()) {
        for (const auto& e : local.errors_json()) {
            errs.add(at(e.value("field", "").c_str()), e.value("code", ""), e.value("message", ""));
        }
        return false;
    }
    out.type = Validation::opt_string(body, "type").value_or("expense");
    out.date = body["date"].get<std::string>();
    out.time = Validation::opt_string(body, "time");
    out.account_id = body["account_id"].get<std::string>();
    out.amount = body["amount"].get<double>();
    out.category_id = Validation::opt_string(body, "category_id");
    out.merchant = Validation::opt_string(body, "merchant").value_or("");
    out.name = body["name"].get<std::string>();
    out.receipt_amount = opt_number(body, "receipt_amount");
    out.receipt_currency = Validation::opt_string(body, "receipt_currency");
    out.fx_note = Validation::opt_string(body, "fx_note").value_or("");
    out.adjusts_id = Validation::opt_string(body, "adjusts_id");
    out.trip = Validation::opt_string(body, "trip").value_or("");
    out.note = Validation::opt_string(body, "note").value_or("");
    out.source = Validation::opt_string(body, "source").value_or("manual");
    out.status = Validation::opt_string(body, "status").value_or("posted");
    out.external_id = Validation::opt_string(body, "external_id");
    return true;
}

Repo::TransactionRepository::Filter transaction_filter(const HttpRequestPtr& req) {
    Repo::TransactionRepository::Filter f;
    f.from = query_param(req, "from");
    f.to = query_param(req, "to");
    f.account_id = query_param(req, "account");
    f.category_id = query_param(req, "category");
    f.currency = query_param(req, "currency");
    f.type = query_param(req, "type");
    f.status = query_param(req, "status");
    f.trip = query_param(req, "trip");
    f.q = req->getParameter("q");
    const auto page = parse_page_params(req, kDefaultLimit, kMaxLimit);
    f.limit = page.limit;
    f.offset = page.offset;
    return f;
}

std::optional<HttpResponsePtr> filter_problem(const Repo::TransactionRepository::Filter& f) {
    if ((f.from && !is_calendar_date(*f.from)) || (f.to && !is_calendar_date(*f.to))) {
        return ErrorResponse::bad_request("invalid_date", "from and to must be calendar days as YYYY-MM-DD");
    }
    if ((f.account_id && !is_valid_uuid(*f.account_id)) || (f.category_id && !is_valid_uuid(*f.category_id))) {
        return ErrorResponse::bad_request("invalid_id", "account and category must be UUIDs");
    }
    if (f.currency && !std::regex_match(*f.currency, kCodeRe)) {
        return ErrorResponse::bad_request("invalid_currency", "currency must be an ISO code");
    }
    if (f.type && std::find(kTypes.begin(), kTypes.end(), *f.type) == kTypes.end()) {
        return ErrorResponse::bad_request("invalid_type", "type must be income, expense or fx_adjustment");
    }
    if (f.status && std::find(kStatuses.begin(), kStatuses.end(), *f.status) == kStatuses.end()) {
        return ErrorResponse::bad_request("invalid_status", "status must be posted or pending");
    }
    return std::nullopt;
}

}  // namespace

#define MONEY_GUARD(req, callback, owner)    \
    if (!require_enabled(callback))          \
        return;                              \
    API_REQUIRE_OWNER(req, callback, owner); \
    if (!require_user(owner, callback))      \
    return

bool MoneyController::require_enabled(const Callback& callback) {
    if (Core::money_enabled()) {
        return true;
    }
    callback(ErrorResponse::not_found("money"));
    return false;
}

// ── currencies ──────────────────────────────────────────────────────────────

void MoneyController::listCurrencies(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    with_repo_errors(callback, "money.listCurrencies", [&] {
        Repo::CurrencyRepository repo;
        repo.seed_defaults(owner);
        callback(Response::ok(json{{"data", repo.list(owner, req->getParameter("archived") == "true")}}));
    });
}

void MoneyController::upsertCurrency(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    seed_currencies(owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "code");
    code_field(errs, body, "code");
    text_length(errs, body, "name", 0, 60);
    if (body.contains("role") && !body["role"].is_null()) {
        Validation::one_of(errs, body, "role", kRoles);
    }
    Validation::int_range(errs, body, "decimals", 0, 8);
    text_length(errs, body, "minor_unit", 0, 30);
    Validation::boolean(errs, body, "archived");
    // A code the table knows takes its smallest unit from it; another code names its own.
    std::optional<Money::Currencies::MinorUnit> iso;
    if (!errs.any()) {
        iso = Money::Currencies::known(body["code"].get<std::string>());
    }
    if (iso.has_value()) {
        if (opt_int(body, "decimals").value_or(iso->decimals) != iso->decimals) {
            errs.add("decimals",
                     "fixed",
                     body["code"].get<std::string>() + " has " + std::to_string(iso->decimals) + " decimal(s)");
        }
    } else if (!errs.any() && !opt_int(body, "decimals").has_value()) {
        errs.add("decimals", "missing", "an unknown currency needs its decimals (0..8)");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::CurrencyRepository::Input in;
    in.name = Validation::opt_string(body, "name").value_or("");
    in.role = Validation::opt_string(body, "role");
    in.decimals = iso.has_value() ? iso->decimals : *opt_int(body, "decimals");
    in.minor_unit = iso.has_value() ? std::string(iso->unit) : Validation::opt_string(body, "minor_unit").value_or("");
    in.archived = body.contains("archived") && body["archived"].is_boolean() && body["archived"].get<bool>();
    with_repo_errors(callback, "money.upsertCurrency", [&] {
        callback(Response::ok(json{{"data", Repo::CurrencyRepository().upsert(owner, body["code"], in)}}));
    });
}

void MoneyController::patchCurrency(const HttpRequestPtr& req, Callback&& callback, const std::string& code) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    text_length(errs, body, "name", 0, 60);
    if (body.contains("role") && !body["role"].is_null()) {
        Validation::one_of(errs, body, "role", kRoles);
    }
    Validation::int_range(errs, body, "decimals", 0, 8);
    text_length(errs, body, "minor_unit", 0, 30);
    Validation::boolean(errs, body, "archived");
    if (const auto iso = Money::Currencies::known(code);
        iso.has_value() && !errs.any() &&
        (opt_int(body, "decimals").value_or(iso->decimals) != iso->decimals ||
         Validation::opt_string(body, "minor_unit").value_or(std::string(iso->unit)) != iso->unit)) {
        errs.add("decimals",
                 "fixed",
                 code + " has " + std::to_string(iso->decimals) + " decimal(s), the " + std::string(iso->unit));
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::CurrencyRepository::Patch p;
    p.minor_unit = Validation::opt_string(body, "minor_unit");
    p.name = Validation::opt_string(body, "name");
    if (body.contains("role")) {
        p.role = Validation::opt_string(body, "role");
    }
    p.decimals = opt_int(body, "decimals");
    if (body.contains("archived") && body["archived"].is_boolean()) {
        p.archived = body["archived"].get<bool>();
    }
    with_repo_errors(callback, "money.patchCurrency", [&] {
        callback(Response::ok(json{{"data", Repo::CurrencyRepository().patch(owner, code, p)}}));
    });
}

// ── accounts ────────────────────────────────────────────────────────────────

namespace {

void account_fields(Validation::Errors& errs, const json& body) {
    text_length(errs, body, "name", 1, 120);
    text_length(errs, body, "bank", 0, 60);
    if (body.contains("kind")) {
        Validation::one_of(errs, body, "kind", kAccountKinds);
    }
    code_field(errs, body, "currency");
    if (body.contains("last4") && !body["last4"].is_null()) {
        Validation::regex_match(errs, body, "last4", kLast4Re, "up to four digits");
    }
    number_range(errs, body, "opening_balance", -kAmountMax, kAmountMax);
    date_field(errs, body, "opening_date");
    Validation::int_range(errs, body, "position", 0, 10000);
    Validation::boolean(errs, body, "archived");
}

}  // namespace

void MoneyController::listAccounts(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    with_repo_errors(callback, "money.listAccounts", [&] {
        callback(Response::ok(
            json{{"data", Repo::AccountRepository().list(owner, req->getParameter("archived") == "true")}}));
    });
}

void MoneyController::createAccount(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    seed_currencies(owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    Validation::require_string(errs, body, "currency");
    account_fields(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::AccountRepository::Input in;
    in.name = body["name"];
    in.bank = Validation::opt_string(body, "bank").value_or("");
    in.kind = Validation::opt_string(body, "kind").value_or("card");
    in.currency = body["currency"];
    in.last4 = Validation::opt_string(body, "last4").value_or("");
    in.opening_balance = opt_number(body, "opening_balance").value_or(0);
    in.opening_date = Validation::opt_string(body, "opening_date");
    in.position = opt_int(body, "position").value_or(0);
    with_repo_errors(callback, "money.createAccount", [&] {
        callback(Response::created(json{{"data", Repo::AccountRepository().create(owner, in)}}));
    });
}

void MoneyController::getAccount(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.getAccount", [&] {
        const auto row = Repo::AccountRepository().find(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("money_account"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MoneyController::updateAccount(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    account_fields(errs, body);
    if (body.contains("currency")) {
        errs.add("currency", "invariant", "the currency of an account does not change: its rows are in it");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::AccountRepository::Patch p;
    p.name = Validation::opt_string(body, "name");
    p.bank = Validation::opt_string(body, "bank");
    p.kind = Validation::opt_string(body, "kind");
    p.last4 = Validation::opt_string(body, "last4");
    p.opening_balance = opt_number(body, "opening_balance");
    if (body.contains("opening_date")) {
        p.opening_date = Validation::opt_string(body, "opening_date");
    }
    if (body.contains("archived") && body["archived"].is_boolean()) {
        p.archived = body["archived"].get<bool>();
    }
    p.position = opt_int(body, "position");
    with_repo_errors(callback, "money.updateAccount", [&] {
        callback(Response::ok(json{{"data", Repo::AccountRepository().update(owner, id, p)}}));
    });
}

void MoneyController::deleteAccount(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.deleteAccount", [&] {
        const std::string outcome = Repo::AccountRepository().remove(owner, id);
        callback(Response::ok(json{{"message", outcome}, {"outcome", outcome}}));
    });
}

// ── categories ──────────────────────────────────────────────────────────────

namespace {

void category_fields(Validation::Errors& errs, const json& body) {
    text_length(errs, body, "name", 1, 60);
    if (body.contains("kind")) {
        Validation::one_of(errs, body, "kind", kKinds);
    }
    if (body.contains("flexibility")) {
        Validation::one_of(errs, body, "flexibility", kFlex);
    }
    number_range(errs, body, "budget_max", 0.0001, kAmountMax);
    code_field(errs, body, "budget_currency");
    const bool has_max = body.contains("budget_max") && !body["budget_max"].is_null();
    const bool has_cur = body.contains("budget_currency") && !body["budget_currency"].is_null();
    if (has_max != has_cur) {
        errs.add("budget_currency", "invariant", "a budget is an amount and a currency together");
    }
    Validation::int_range(errs, body, "position", 0, 10000);
    Validation::boolean(errs, body, "archived");
}

}  // namespace

void MoneyController::listCategories(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    with_repo_errors(callback, "money.listCategories", [&] {
        callback(Response::ok(
            json{{"data", Repo::CategoryRepository().list(owner, req->getParameter("archived") == "true")}}));
    });
}

void MoneyController::createCategory(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    seed_currencies(owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "name");
    category_fields(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::CategoryRepository::Input in;
    in.name = body["name"];
    in.kind = Validation::opt_string(body, "kind").value_or("expense");
    in.flexibility = Validation::opt_string(body, "flexibility").value_or("variable");
    in.budget_max = opt_number(body, "budget_max");
    in.budget_currency = Validation::opt_string(body, "budget_currency");
    in.position = opt_int(body, "position").value_or(0);
    with_repo_errors(callback, "money.createCategory", [&] {
        callback(Response::created(json{{"data", Repo::CategoryRepository().create(owner, in)}}));
    });
}

void MoneyController::getCategory(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.getCategory", [&] {
        const auto row = Repo::CategoryRepository().find(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("money_category"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MoneyController::updateCategory(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    category_fields(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::CategoryRepository::Patch p;
    p.name = Validation::opt_string(body, "name");
    p.kind = Validation::opt_string(body, "kind");
    p.flexibility = Validation::opt_string(body, "flexibility");
    if (body.contains("budget_max") || body.contains("budget_currency")) {
        p.budget_max = opt_number(body, "budget_max");
        p.budget_currency = Validation::opt_string(body, "budget_currency");
    }
    if (body.contains("archived") && body["archived"].is_boolean()) {
        p.archived = body["archived"].get<bool>();
    }
    p.position = opt_int(body, "position");
    with_repo_errors(callback, "money.updateCategory", [&] {
        callback(Response::ok(json{{"data", Repo::CategoryRepository().update(owner, id, p)}}));
    });
}

void MoneyController::deleteCategory(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.deleteCategory", [&] {
        const std::string outcome = Repo::CategoryRepository().remove(owner, id);
        callback(Response::ok(json{{"message", outcome}, {"outcome", outcome}}));
    });
}

// ── transactions ────────────────────────────────────────────────────────────

void MoneyController::listTransactions(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    const auto f = transaction_filter(req);
    if (const auto problem = filter_problem(f)) {
        callback(*problem);
        return;
    }
    with_repo_errors(callback, "money.listTransactions", [&] {
        const auto page = Repo::TransactionRepository().list(owner, f);
        callback(Response::paginated(page.rows, page.total, static_cast<int>(f.limit), static_cast<int>(f.offset)));
    });
}

void MoneyController::createTransaction(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Repo::TransactionRepository::Input in;
    if (!transaction_input(body, "", errs, in)) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "money.createTransaction", [&] {
        callback(Response::created(json{{"data", Repo::TransactionRepository().create(owner, in)}}));
    });
}

void MoneyController::createTransactions(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    if (!body.contains("transactions") || !body["transactions"].is_array() || body["transactions"].empty() ||
        body["transactions"].size() > kBatchMax) {
        callback(ErrorResponse::bad_request("invalid_batch", "transactions must hold 1..100 rows"));
        return;
    }
    Validation::Errors errs;
    std::vector<Repo::TransactionRepository::Input> inputs;
    for (std::size_t i = 0; i < body["transactions"].size(); ++i) {
        Repo::TransactionRepository::Input in;
        if (transaction_input(body["transactions"][i], "transactions[" + std::to_string(i) + "].", errs, in)) {
            inputs.push_back(in);
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "money.createTransactions", [&] {
        const json rows = Repo::TransactionRepository().create_many(owner, inputs);
        callback(Response::created(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void MoneyController::getTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.getTransaction", [&] {
        const auto row = Repo::TransactionRepository().find(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("money_transaction"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MoneyController::updateTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    date_field(errs, body, "date");
    if (body.contains("time") && !body["time"].is_null()) {
        Validation::regex_match(errs, body, "time", std::regex(R"(^([01]\d|2[0-3]):[0-5]\d$)"), "expected HH:MM");
    }
    uuid_field(errs, body, "account_id");
    number_range(errs, body, "amount", -kAmountMax, kAmountMax);
    uuid_field(errs, body, "category_id");
    text_length(errs, body, "merchant", 0, kNameMax);
    text_length(errs, body, "name", 1, kNameMax);
    number_range(errs, body, "receipt_amount", 0.0001, kAmountMax);
    code_field(errs, body, "receipt_currency");
    text_length(errs, body, "fx_note", 0, 200);
    text_length(errs, body, "trip", 0, 60);
    text_length(errs, body, "note", 0, kNoteMax);
    if (body.contains("receipt_amount") != body.contains("receipt_currency") ||
        (body.contains("receipt_amount") && body["receipt_amount"].is_null() != body["receipt_currency"].is_null())) {
        errs.add("receipt_currency", "invariant", "the receipt's amount and currency go together");
    }
    for (const char* fixed : {"type", "adjusts_id", "status", "source"}) {
        if (body.contains(fixed)) {
            errs.add(fixed, "invariant", "cannot change on an existing row");
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::TransactionRepository::Patch p;
    p.date = Validation::opt_string(body, "date");
    if (body.contains("time")) {
        p.time = Validation::opt_string(body, "time");
    }
    p.account_id = Validation::opt_string(body, "account_id");
    p.amount = opt_number(body, "amount");
    p.category_id = Validation::opt_string(body, "category_id");
    p.merchant = Validation::opt_string(body, "merchant");
    p.name = Validation::opt_string(body, "name");
    if (body.contains("receipt_amount") || body.contains("receipt_currency")) {
        p.receipt_amount = opt_number(body, "receipt_amount");
        p.receipt_currency = Validation::opt_string(body, "receipt_currency");
    }
    p.fx_note = Validation::opt_string(body, "fx_note");
    p.trip = Validation::opt_string(body, "trip");
    p.note = Validation::opt_string(body, "note");
    with_repo_errors(callback, "money.updateTransaction", [&] {
        callback(Response::ok(json{{"data", Repo::TransactionRepository().update(owner, id, p)}}));
    });
}

void MoneyController::deleteTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.deleteTransaction", [&] {
        Repo::TransactionRepository().remove(owner, id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

void MoneyController::confirmTransaction(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.confirmTransaction", [&] {
        callback(Response::ok(json{{"data", Repo::TransactionRepository().confirm(owner, id)}}));
    });
}

void MoneyController::inbox(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    with_repo_errors(callback, "money.inbox", [&] {
        const json rows = Repo::TransactionRepository().inbox(owner);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

// ── transfers ───────────────────────────────────────────────────────────────

void MoneyController::listTransfers(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    Repo::TransferRepository::Filter f;
    f.from = query_param(req, "from");
    f.to = query_param(req, "to");
    f.account_id = query_param(req, "account");
    const auto page = parse_page_params(req, kDefaultLimit, kMaxLimit);
    f.limit = page.limit;
    f.offset = page.offset;
    if ((f.from && !is_calendar_date(*f.from)) || (f.to && !is_calendar_date(*f.to))) {
        callback(ErrorResponse::bad_request("invalid_date", "from and to must be calendar days as YYYY-MM-DD"));
        return;
    }
    if (f.account_id && !is_valid_uuid(*f.account_id)) {
        callback(ErrorResponse::bad_request("invalid_id", "account must be a UUID"));
        return;
    }
    with_repo_errors(callback, "money.listTransfers", [&] {
        const json rows = Repo::TransferRepository().list(owner, f);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

namespace {

void transfer_fields(Validation::Errors& errs, const json& body) {
    date_field(errs, body, "date");
    uuid_field(errs, body, "from_account_id");
    uuid_field(errs, body, "to_account_id");
    number_range(errs, body, "amount_sent", 0.0001, kAmountMax);
    number_range(errs, body, "amount_received", 0.0001, kAmountMax);
    number_range(errs, body, "fee", 0, kAmountMax);
    text_length(errs, body, "name", 0, kNameMax);
    text_length(errs, body, "note", 0, kNoteMax);
    text_length(errs, body, "external_id", 1, 120);
}

}  // namespace

void MoneyController::createTransfer(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "date");
    Validation::require_string(errs, body, "from_account_id");
    Validation::require_string(errs, body, "to_account_id");
    Validation::require(errs, body, "amount_sent");
    transfer_fields(errs, body);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::TransferRepository::Input in;
    in.date = body["date"];
    in.from_account_id = body["from_account_id"];
    in.to_account_id = body["to_account_id"];
    in.amount_sent = body["amount_sent"].get<double>();
    in.amount_received = opt_number(body, "amount_received");
    in.fee = opt_number(body, "fee");
    in.name = Validation::opt_string(body, "name").value_or("");
    in.note = Validation::opt_string(body, "note").value_or("");
    in.external_id = Validation::opt_string(body, "external_id");
    with_repo_errors(callback, "money.createTransfer", [&] {
        callback(Response::created(json{{"data", Repo::TransferRepository().create(owner, in)}}));
    });
}

void MoneyController::getTransfer(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.getTransfer", [&] {
        const auto row = Repo::TransferRepository().find(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("money_transfer"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MoneyController::updateTransfer(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    transfer_fields(errs, body);
    for (const char* fixed : {"from_account_id", "to_account_id"}) {
        if (body.contains(fixed)) {
            errs.add(fixed, "invariant", "the accounts of a transfer do not change: make a new one");
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::TransferRepository::Patch p;
    p.date = Validation::opt_string(body, "date");
    p.amount_sent = opt_number(body, "amount_sent");
    if (body.contains("amount_received")) {
        p.amount_received = opt_number(body, "amount_received");
    }
    if (body.contains("fee")) {
        p.fee = opt_number(body, "fee");
    }
    p.name = Validation::opt_string(body, "name");
    p.note = Validation::opt_string(body, "note");
    with_repo_errors(callback, "money.updateTransfer", [&] {
        callback(Response::ok(json{{"data", Repo::TransferRepository().update(owner, id, p)}}));
    });
}

void MoneyController::deleteTransfer(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.deleteTransfer", [&] {
        Repo::TransferRepository().remove(owner, id);
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k204NoContent);
        callback(resp);
    });
}

// ── merchants ───────────────────────────────────────────────────────────────

void MoneyController::merchants(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    const int limit = clamp_int(req->getParameter("limit"), 20, 1, 300);
    with_repo_errors(callback, "money.merchants", [&] {
        const json rows = Repo::MerchantRepository().suggest(owner, req->getParameter("q"), limit);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void MoneyController::patchMerchant(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    // The key travels in the body: it holds spaces, `&`, `'` and any script,
    // which a path segment would have to escape.
    Validation::Errors errs;
    Validation::require_string(errs, body, "merchant_key");
    text_length(errs, body, "merchant_key", 1, 200);
    uuid_field(errs, body, "category_id");
    if (!body.contains("category_id")) {
        errs.add("category_id", "missing", "category_id (or null) is required");
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    // A display name ("Big C") and its key ("big c") name the same merchant.
    const std::string key = Repo::MerchantRepository::key(body["merchant_key"].get<std::string>());
    with_repo_errors(callback, "money.patchMerchant", [&] {
        callback(Response::ok(
            json{{"data",
                  Repo::MerchantRepository().set_category(owner, key, Validation::opt_string(body, "category_id"))}}));
    });
}

// ── rates ───────────────────────────────────────────────────────────────────

namespace {

json rate_json(const Repo::FxRateRepository::Rate& r) {
    return {{"quote", r.quote}, {"per_usd", r.per_usd}, {"rate_date", r.date}, {"source", "fawazahmed0/currency-api"}};
}

}  // namespace

void MoneyController::rate(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
    const std::string quote = req->getParameter("quote");
    if (!is_calendar_date(date) || !std::regex_match(quote, kCodeRe)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day and quote an ISO code"));
        return;
    }
    with_repo_errors(callback, "money.rate", [&] {
        const auto r = Repo::FxRateRepository().nearest(date, quote);
        if (!r) {
            callback(ErrorResponse::not_found("no_rate"));
            return;
        }
        callback(Response::ok(json{{"data", rate_json(*r)}}));
    });
}

void MoneyController::convert(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    const std::string date = req->getParameter("date").empty() ? today_utc() : req->getParameter("date");
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    double amount = 0;
    try {
        const std::string text = req->getParameter("amount");
        std::size_t used = 0;
        amount = std::stod(text, &used);
        if (used != text.size() || !std::isfinite(amount)) {
            throw std::invalid_argument("not a number");
        }
    } catch (const std::exception&) {
        callback(ErrorResponse::bad_request("invalid_query", "amount must be a number"));
        return;
    }
    if (!is_calendar_date(date) || !std::regex_match(from, kCodeRe) || !std::regex_match(to, kCodeRe)) {
        callback(ErrorResponse::bad_request("invalid_query", "date must be a calendar day, from and to ISO codes"));
        return;
    }
    with_repo_errors(callback, "money.convert", [&] {
        Repo::FxRateRepository rates;
        const auto f = rates.nearest(date, from);
        const auto t = rates.nearest(date, to);
        if (!f || !t) {
            callback(ErrorResponse::not_found("no_rate"));
            return;
        }
        const Money::Rates::Rate rf{f->date, f->quote, f->per_usd};
        const Money::Rates::Rate rt{t->date, t->quote, t->per_usd};
        const auto c = Money::Rates::convert(amount, &rf, &rt);
        if (!c) {
            callback(ErrorResponse::not_found("no_rate"));
            return;
        }
        callback(Response::ok(json{{"data",
                                    {{"amount", c->amount},
                                     {"from", from},
                                     {"to", to},
                                     {"rate_date", c->rate_date},
                                     {"rates", json{{from, rate_json(*f)}, {to, rate_json(*t)}}}}}}));
    });
}

void MoneyController::refreshRates(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    // No body or {}: today's rates. {from, to}: one job per day of the range that has none yet,
    // refused while an earlier backfill still waits in the queue (its days would be queued twice).
    std::optional<std::string> from;
    std::string to = today_utc();
    json body = json::object();
    if (!req->body().empty() && !parse_object(req, body, callback)) {
        return;
    }
    if (!body.empty()) {
        Validation::Errors errs;
        Validation::require_string(errs, body, "from");
        date_field(errs, body, "from");
        date_field(errs, body, "to");
        if (errs.any()) {
            callback(Validation::response_400(errs));
            return;
        }
        from = body["from"].get<std::string>();
        if (body.contains("to") && body["to"].is_string()) {
            to = body["to"].get<std::string>();
        }
        if (*from < kRatesHistoryStart || *from > to || to > today_utc()) {
            callback(ErrorResponse::bad_request(
                "invalid_range", std::string("from..to must lie between ") + kRatesHistoryStart + " and today"));
            return;
        }
    }
    if (!Jobs::is_initialized()) {
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    try {
        if (from.has_value()) {
            const auto days = Jobs::MoneyRates::missing_days(*from, to);
            if (const long waiting = days.empty() ? 0 : Jobs::MoneyRates::waiting(); waiting > 0) {
                callback(ErrorResponse::conflict(
                    "backfill_running",
                    std::to_string(waiting) + " rate job(s) still wait in the queue; retry when they finish"));
                return;
            }
            const int queued = Jobs::MoneyRates::enqueue_days(days);
            auto resp = Response::ok(json{{"data", {{"queued", queued}, {"from", *from}, {"to", to}}}});
            resp->setStatusCode(k202Accepted);
            callback(resp);
            return;
        }
        const auto job = Jobs::get().submit(Jobs::MoneyRates::kJobType, json{{"date", "latest"}});
        auto resp = Response::ok(json{{"data", {{"job_id", job.id}, {"status", "queued"}}}});
        resp->setStatusCode(k202Accepted);
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::warn("money rates refresh enqueue failed: {}", e.what());
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
    }
}

// ── reports ─────────────────────────────────────────────────────────────────

void MoneyController::periodReport(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    const std::string kind_text = req->getParameter("kind").empty() ? "month" : req->getParameter("kind");
    if (std::find(kPeriodKinds.begin(), kPeriodKinds.end(), kind_text) == kPeriodKinds.end()) {
        callback(ErrorResponse::bad_request("invalid_kind", "kind must be week, month, quarter or custom"));
        return;
    }
    const std::string today = today_utc();
    const std::string anchor = req->getParameter("date").empty() ? today : req->getParameter("date");
    const std::string from = req->getParameter("from");
    const std::string to = req->getParameter("to");
    const std::string as_if = req->getParameter("as_if");
    Money::Period::Kind kind = Money::Period::Kind::month;
    if (kind_text == "week") {
        kind = Money::Period::Kind::week;
    } else if (kind_text == "quarter") {
        kind = Money::Period::Kind::quarter;
    } else if (kind_text == "custom") {
        kind = Money::Period::Kind::custom;
    }
    Money::Period::Range range;
    try {
        if (kind == Money::Period::Kind::custom) {
            if (!is_calendar_date(from) || !is_calendar_date(to)) {
                callback(ErrorResponse::bad_request("invalid_date", "a custom period needs from and to"));
                return;
            }
            range = Money::Period::custom(from, to);
        } else {
            if (!is_calendar_date(anchor)) {
                callback(ErrorResponse::bad_request("invalid_date", "date must be a calendar day as YYYY-MM-DD"));
                return;
            }
            range = Money::Period::of(kind, anchor);
        }
    } catch (const std::invalid_argument& e) {
        callback(ErrorResponse::bad_request("invalid_period", e.what()));
        return;
    }
    if (!as_if.empty() && !std::regex_match(as_if, kCodeRe)) {
        callback(ErrorResponse::bad_request("invalid_currency", "as_if must be an ISO code"));
        return;
    }
    with_repo_errors(callback, "money.periodReport", [&] {
        const std::string target = as_if.empty() ? view_currency(owner).value_or("") : as_if;
        const json out = Repo::ReportBuilder::build(owner, kind, kind_text, range, today, target);
        callback(Response::ok(json{{"data", out}}));
    });
}

void MoneyController::balances(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    with_repo_errors(callback, "money.balances", [&] {
        const json accounts = Repo::AccountRepository().list(owner, req->getParameter("archived") == "true");
        std::map<std::string, json> groups;
        for (const auto& a : accounts) {
            const std::string c = a["currency"];
            if (!groups.count(c)) {
                groups[c] = json{{"currency", c}, {"total", 0.0}, {"accounts", json::array()}};
            }
            groups[c]["total"] = groups[c]["total"].get<double>() + a["balance"].get<double>();
            groups[c]["accounts"].push_back(a);
        }
        json out = json::array();
        for (const auto& [c, g] : groups) {
            out.push_back(g);
        }
        callback(Response::ok(json{{"data", out}}));
    });
}

// ── parse ───────────────────────────────────────────────────────────────────

namespace {

/// "data:image/<type>;base64,<payload>" -> {type, payload}; nullopt with a reason.
struct DataUrl {
    std::string type;
    std::string payload;
};

std::optional<DataUrl> data_url(const std::string& url, std::string& problem) {
    static const std::vector<std::string> kImageTypes = {"image/jpeg", "image/png", "image/webp"};
    const std::string prefix = "data:";
    const auto comma = url.find(',');
    if (url.rfind(prefix, 0) != 0 || comma == std::string::npos) {
        problem = "expected a data URL: data:image/jpeg;base64,...";
        return std::nullopt;
    }
    const std::string meta = url.substr(prefix.size(), comma - prefix.size());
    const auto semi = meta.find(";base64");
    if (semi == std::string::npos || semi + 7 != meta.size()) {
        problem = "the data URL must be base64";
        return std::nullopt;
    }
    DataUrl out{meta.substr(0, semi), url.substr(comma + 1)};
    if (std::find(kImageTypes.begin(), kImageTypes.end(), out.type) == kImageTypes.end()) {
        problem = "the image must be jpeg, png or webp";
        return std::nullopt;
    }
    if (out.payload.empty() || out.payload.size() % 4 != 0) {
        problem = "the base64 payload is broken";
        return std::nullopt;
    }
    std::size_t padding = 0;
    for (std::size_t i = 0; i < out.payload.size(); ++i) {
        const char c = out.payload[i];
        const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c == '=') {
            if (i + 2 < out.payload.size()) {
                problem = "the base64 payload is broken";
                return std::nullopt;
            }
            ++padding;
        } else if (!alnum && c != '+' && c != '/') {
            problem = "the base64 payload is broken";
            return std::nullopt;
        } else if (padding > 0) {
            problem = "the base64 payload is broken";
            return std::nullopt;
        }
    }
    if (out.payload.size() / 4 * 3 - padding > kImageMaxBytes) {
        problem = "the image is larger than 4 MB";
        return std::nullopt;
    }
    return out;
}

/// The shared tail of both parse routes: the cap, the row, the job.
template <typename Create>
void start_parse(const std::string& owner, MoneyController::Callback& callback, Create&& create) {
    if (!Money::Llm::parse_settings().has_value()) {
        callback(ErrorResponse::service_unavailable("not_configured", "the money parse is not set up on this server"));
        return;
    }
    Repo::ParseJobRepository repo;
    std::string id;
    try {
        if (repo.open_count(owner) >= kParseOpenMax) {
            callback(ErrorResponse::make({k429TooManyRequests,
                                          "too_many_parses",
                                          "wait for your running parses to finish",
                                          json{{"retry_after_sec", 30}}}));
            return;
        }
        id = create(repo)["id"].template get<std::string>();
    } catch (const Repositories::ValidationError& e) {
        callback(ErrorResponse::bad_request(e.code(), e.message()));
        return;
    } catch (const pqxx::sql_error& e) {
        // The server's DETAIL would quote the row (the bank text, the photo): the state only.
        spdlog::warn("money parse create failed: SQLSTATE {}", e.sqlstate());
        callback(ErrorResponse::service_unavailable("storage_unavailable"));
        return;
    } catch (const std::exception&) {
        spdlog::warn("money parse create failed");
        callback(ErrorResponse::service_unavailable("storage_unavailable"));
        return;
    }
    try {
        if (!Jobs::is_initialized()) {
            throw std::runtime_error("the job queue is not initialized");
        }
        Jobs::get().submit(
            Jobs::MoneyParse::kJobType,
            json{{"job_id", id}, {"owner_id", owner}, {"max_attempts", Jobs::get().default_max_retries()}});
    } catch (const std::exception& e) {
        spdlog::warn("money parse enqueue unavailable: {}", e.what());
        try {
            repo.fail(id, "queue_unavailable", "the job queue did not take the job");
        } catch (const std::exception& inner) {
            spdlog::warn("money parse {}: could not mark failed: {}", id, inner.what());
        }
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    auto resp = Response::ok(json{{"data", {{"id", id}, {"status", "queued"}}}});
    resp->setStatusCode(k202Accepted);
    callback(resp);
}

}  // namespace

void MoneyController::parseText(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "text");
    text_length(errs, body, "text", 1, kParseTextMax);
    date_field(errs, body, "hint_date");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::string text = body["text"].get<std::string>();
    if (text.find('\0') != std::string::npos) {
        errs.add("text", "invalid", "must not contain a NUL character");
        callback(Validation::response_400(errs));
        return;
    }
    const std::string hint = Validation::opt_string(body, "hint_date").value_or(today_utc());
    start_parse(owner, callback, [&](Repo::ParseJobRepository& repo) { return repo.create_text(owner, text, hint); });
}

void MoneyController::parseReceipt(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    Validation::require_string(errs, body, "image");
    date_field(errs, body, "hint_date");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    std::string problem;
    const auto image = data_url(body["image"].get<std::string>(), problem);
    if (!image) {
        errs.add("image", "invalid", problem);
        callback(Validation::response_400(errs));
        return;
    }
    const std::string hint = Validation::opt_string(body, "hint_date").value_or(today_utc());
    start_parse(owner, callback, [&](Repo::ParseJobRepository& repo) {
        return repo.create_receipt(owner, image->payload, image->type, hint);
    });
}

void MoneyController::parseStatus(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.parseStatus", [&] {
        const auto row = Repo::ParseJobRepository().get(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("money_parse"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MoneyController::parseAccept(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    if (!body.contains("lines") || !body["lines"].is_array() || body["lines"].empty() || body["lines"].size() > 100) {
        callback(ErrorResponse::bad_request("invalid_lines", "lines must hold 1..100 rows"));
        return;
    }
    std::optional<json> job;
    if (!with_repo_errors(
            callback, "money.parseAccept.load", [&] { job = Repo::ParseJobRepository().get(owner, id); })) {
        return;
    }
    if (!job) {
        callback(ErrorResponse::not_found("money_parse"));
        return;
    }
    Validation::Errors errs;
    std::vector<Repo::TransactionRepository::Input> inputs;
    for (std::size_t i = 0; i < body["lines"].size(); ++i) {
        json line = body["lines"][i];
        if (line.is_object()) {
            // The inbox holds what a parse proposed; the user posts each row.
            line.erase("status");
            line.erase("source");
            line.erase("adjusts_id");
            if (line.contains("type") && line["type"] == "fx_adjustment") {
                errs.add("lines[" + std::to_string(i) + "].type", "invalid", "a parse proposes incomes and expenses");
                continue;
            }
        }
        Repo::TransactionRepository::Input in;
        if (transaction_input(line, "lines[" + std::to_string(i) + "].", errs, in)) {
            in.status = "pending";
            in.source = (*job)["kind"] == "receipt" ? "receipt" : "text";
            inputs.push_back(in);
        }
    }
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    with_repo_errors(callback, "money.parseAccept", [&] {
        const json rows = Repo::TransactionRepository().accept_parse(owner, id, inputs);
        callback(Response::created(json{{"data", rows}, {"count", rows.size()}}));
    });
}

// ── advisor ─────────────────────────────────────────────────────────────────

void MoneyController::advisorReports(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    const int limit = clamp_int(req->getParameter("limit"), 20, 1, 100);
    with_repo_errors(callback, "money.advisorReports", [&] {
        const json rows = Repo::AdvisorReportRepository().list(owner, limit);
        callback(Response::ok(json{{"data", rows}, {"count", rows.size()}}));
    });
}

void MoneyController::advisorReport(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    MONEY_GUARD(req, callback, owner);
    if (!require_valid_uuid(id, callback)) {
        return;
    }
    with_repo_errors(callback, "money.advisorReport", [&] {
        const auto row = Repo::AdvisorReportRepository().get(owner, id);
        if (!row) {
            callback(ErrorResponse::not_found("money_advisor_report"));
            return;
        }
        callback(Response::ok(json{{"data", *row}}));
    });
}

void MoneyController::advisorRun(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    if (body.contains("period")) {
        Validation::one_of(errs, body, "period", {"week", "month", "quarter"});
    }
    date_field(errs, body, "date");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    if (!Money::Llm::advisor_settings().has_value()) {
        callback(ErrorResponse::service_unavailable("not_configured", "the advisor is not set up on this server"));
        return;
    }
    if (!Jobs::is_initialized()) {
        callback(ErrorResponse::service_unavailable("queue_unavailable"));
        return;
    }
    const std::string period = Validation::opt_string(body, "period").value_or("week");
    const std::string date = Validation::opt_string(body, "date").value_or(today_utc());
    const auto kind = period == "week"      ? Money::Period::Kind::week
                      : period == "quarter" ? Money::Period::Kind::quarter
                                            : Money::Period::Kind::month;
    const auto range = Money::Period::of(kind, date);
    with_repo_errors(callback, "money.advisorRun", [&] {
        const auto [row, queued] = Repo::AdvisorReportRepository().create_or_get(owner, period, range.from, range.to);
        if (queued) {
            Jobs::get().submit(Jobs::MoneyAdvisor::kJobType,
                               json{{"report_id", row["id"]},
                                    {"owner_id", owner},
                                    {"max_attempts", Jobs::get().default_max_retries()}});
        }
        auto resp = Response::ok(json{{"data", row}, {"queued", queued}});
        resp->setStatusCode(queued ? k202Accepted : k200OK);
        callback(resp);
    });
}

// ── settings ────────────────────────────────────────────────────────────────

void MoneyController::getSettings(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    with_repo_errors(callback, "money.getSettings", [&] {
        json data = Repo::SettingsRepository().load(owner);
        data["llm_available"] = Money::Llm::parse_settings().has_value();
        callback(Response::ok(json{{"data", data}}));
    });
}

void MoneyController::putSettings(const HttpRequestPtr& req, Callback&& callback) {
    MONEY_GUARD(req, callback, owner);
    seed_currencies(owner);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    Validation::Errors errs;
    code_field(errs, body, "view_currency");
    Validation::boolean(errs, body, "advisor_enabled");
    Validation::int_range(errs, body, "advisor_weekday", 1, 7);
    if (body.contains("advisor_currencies")) {
        if (!body["advisor_currencies"].is_array() || body["advisor_currencies"].size() > 20) {
            errs.add("advisor_currencies", "invalid", "must be an array of up to 20 ISO codes");
        } else {
            for (const auto& c : body["advisor_currencies"]) {
                if (!c.is_string() || !std::regex_match(c.get<std::string>(), kCodeRe)) {
                    errs.add("advisor_currencies", "invalid", "each must be an ISO code like KZT");
                    break;
                }
            }
        }
    }
    text_length(errs, body, "advisor_note", 0, 2000);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    Repo::SettingsRepository::Input in;
    in.view_currency = Validation::opt_string(body, "view_currency");
    in.advisor_enabled =
        body.contains("advisor_enabled") && body["advisor_enabled"].is_boolean() && body["advisor_enabled"].get<bool>();
    in.advisor_weekday = opt_int(body, "advisor_weekday").value_or(1);
    if (body.contains("advisor_currencies") && body["advisor_currencies"].is_array()) {
        for (const auto& c : body["advisor_currencies"]) {
            in.advisor_currencies.push_back(c.get<std::string>());
        }
    }
    in.advisor_note = Validation::opt_string(body, "advisor_note").value_or("");
    with_repo_errors(callback, "money.putSettings", [&] {
        callback(Response::ok(json{{"data", Repo::SettingsRepository().put(owner, in)}}));
    });
}

}  // namespace Api
