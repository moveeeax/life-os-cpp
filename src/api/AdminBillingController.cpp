/**
 * @file AdminBillingController.cpp
 * @brief Bodies for src/api/AdminBillingController.hpp — compiled once into
 *        app_core. Contract and money-critical notes are documented on the
 *        declarations in the header.
 */

#include "api/AdminBillingController.hpp"

#include <cstdint>
#include <limits>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "api/Validation.hpp"
#include "billing/Wallet.hpp"
#include "domain/Billing.hpp"
#include "email/BillingEmails.hpp"
#include "repositories/BillingMetricsRepository.hpp"
#include "repositories/BillingRepository.hpp"
#include "repositories/UserRepository.hpp"
#include "security/Audit.hpp"
#include "security/Auth.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Modules.hpp"

namespace Api {

using json = nlohmann::json;

void AdminBillingController::listPayments(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    const auto page = parse_page_params(req, /*default_limit=*/50, /*max_limit=*/200);

    Repositories::PaymentRepository::Filters f;
    const auto status_param = req->getParameter("status");
    if (!status_param.empty())
        f.status = status_param;
    const auto user_id_param = req->getParameter("user_id");
    if (!user_id_param.empty()) {
        if (!is_valid_uuid(user_id_param)) {
            callback(ErrorResponse::bad_request("invalid_user_id", "user_id filter is not a valid UUID"));
            return;
        }
        f.user_id = user_id_param;
    }

    with_repo_errors(callback, "admin billing listPayments", [&] {
        Repositories::PaymentRepository repo;
        auto result = repo.list_filtered(f, page.limit, page.offset);
        callback(Response::paginated(to_json_array(result.entries), result.total, page.limit, page.offset));
    });
}

void AdminBillingController::listPackages(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    with_repo_errors(callback, "admin billing listPackages", [&] {
        Repositories::PackageRepository repo;
        // CrudBase::list defaults to LIMIT 100; the catalogue is small but
        // pass a high cap so it isn't silently truncated — same reasoning
        // as AdminController::listRoles.
        callback(Response::list(to_json_array(repo.list(1000))));
    });
}

void AdminBillingController::createPackage(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    json body;
    if (!Validation::parse_body(req, body, callback))
        return;

    Validation::Errors errs;
    Validation::require(errs, body, "title");
    Validation::string_length(errs, body, "title", 1, 200);
    Validation::require(errs, body, "amount_cents");
    if (body.contains("amount_cents") && !body["amount_cents"].is_null() &&
        (!body["amount_cents"].is_number_integer() || body["amount_cents"].get<std::int64_t>() <= 0))
        errs.add("amount_cents", "invalid", "amount_cents must be a positive integer");
    Validation::require(errs, body, "credits");
    if (body.contains("credits") && !body["credits"].is_null() &&
        (!body["credits"].is_number_integer() || body["credits"].get<std::int64_t>() <= 0))
        errs.add("credits", "invalid", "credits must be a positive integer");
    else
        Validation::int_range(errs, body, "credits", 1, Billing::kMaxCredits);
    Validation::boolean(errs, body, "active");
    Validation::int_range(errs, body, "sort", std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const bool active = !body.contains("active") || body["active"].is_null() || body["active"].get<bool>();
    const int sort = body.contains("sort") && !body["sort"].is_null() ? body["sort"].get<int>() : 0;

    with_repo_errors(callback, "admin billing createPackage", [&] {
        Repositories::PackageRepository repo;
        auto created = repo.create(body["title"].get<std::string>(),
                                   body["amount_cents"].get<std::int64_t>(),
                                   body["credits"].get<std::int64_t>(),
                                   active,
                                   sort);
        Security::Audit::record(actor_of(req),
                                "billing.package.create",
                                "billing_package",
                                created.id,
                                {{"title", created.title}, {"amount_cents", created.amount_cents}});
        callback(Response::created({{"data", json(created)}}));
    });
}

void AdminBillingController::updatePackage(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           const std::string& id) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    json body;
    if (!Validation::parse_body(req, body, callback))
        return;

    std::optional<std::string> title;
    std::optional<std::int64_t> amount_cents;
    std::optional<std::int64_t> credits;
    std::optional<bool> active;
    std::optional<int> sort;

    Validation::Errors errs;
    if (body.contains("title") && !body["title"].is_null()) {
        Validation::string_length(errs, body, "title", 1, 200);
        if (!errs.any())
            title = body["title"].get<std::string>();
    }
    if (body.contains("amount_cents") && !body["amount_cents"].is_null()) {
        if (!body["amount_cents"].is_number_integer() || body["amount_cents"].get<std::int64_t>() <= 0)
            errs.add("amount_cents", "invalid", "amount_cents must be a positive integer");
        else
            amount_cents = body["amount_cents"].get<std::int64_t>();
    }
    if (body.contains("credits") && !body["credits"].is_null()) {
        if (!body["credits"].is_number_integer() || body["credits"].get<std::int64_t>() <= 0)
            errs.add("credits", "invalid", "credits must be a positive integer");
        else if (body["credits"].get<std::int64_t>() > Billing::kMaxCredits)
            errs.add("credits", "above_max", "max " + std::to_string(Billing::kMaxCredits));
        else
            credits = body["credits"].get<std::int64_t>();
    }
    if (body.contains("active") && body["active"].is_boolean())
        active = body["active"].get<bool>();
    // get<int>() would truncate 4294967297 to 1: the range is checked first.
    Validation::int_range(errs, body, "sort", std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    if (!errs.any() && body.contains("sort") && body["sort"].is_number_integer())
        sort = body["sort"].get<int>();
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    if (!title && !amount_cents && !credits && !active && !sort) {
        callback(ErrorResponse::bad_request("empty_patch",
                                            "Provide at least one of title / amount_cents / credits / active / sort"));
        return;
    }

    with_repo_errors(callback, "admin billing updatePackage", [&] {
        Repositories::PackageRepository repo;
        // The row before the write goes into the audit, so a price change
        // can be reconstructed from the log.
        const auto before = repo.find(id, /*from_primary=*/true);
        auto updated = repo.update(id, title, amount_cents, credits, active, sort);
        Security::Audit::record(actor_of(req),
                                "billing.package.update",
                                "billing_package",
                                id,
                                {{"before", before ? json(*before) : json(nullptr)}, {"after", json(updated)}});
        callback(Response::ok({{"data", json(updated)}}));
    });
}

void AdminBillingController::deletePackage(const HttpRequestPtr& req,
                                           std::function<void(const HttpResponsePtr&)>&& callback,
                                           const std::string& id) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    with_repo_errors(callback, "admin billing deletePackage", [&] {
        Repositories::PackageRepository repo;
        const auto before = repo.find(id, /*from_primary=*/true);
        repo.remove(id);  // throws PackageNotFound -> 404
        Security::Audit::record(actor_of(req),
                                "billing.package.delete",
                                "billing_package",
                                id,
                                {{"before", before ? json(*before) : json(nullptr)}});
        callback(Response::ok({{"message", "Package deleted"}}));
    });
}

void AdminBillingController::getSettings(const HttpRequestPtr& req,
                                         std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    with_repo_errors(callback, "admin billing getSettings", [&] {
        Repositories::BillingSettingsRepository repo;
        auto s = repo.get();
        callback(Response::ok({{"data", json(s)}}));
    });
}

void AdminBillingController::updateSettings(const HttpRequestPtr& req,
                                            std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    json body;
    if (!Validation::parse_body(req, body, callback))
        return;

    Validation::Errors errs;
    Validation::require(errs, body, "credits_per_unit");
    if (body.contains("credits_per_unit") && !body["credits_per_unit"].is_null() &&
        (!body["credits_per_unit"].is_number_integer() || body["credits_per_unit"].get<std::int64_t>() <= 0))
        errs.add("credits_per_unit", "invalid", "credits_per_unit must be a positive integer");
    Validation::require(errs, body, "min_amount_cents");
    if (body.contains("min_amount_cents") && !body["min_amount_cents"].is_null() &&
        (!body["min_amount_cents"].is_number_integer() || body["min_amount_cents"].get<std::int64_t>() <= 0))
        errs.add("min_amount_cents", "invalid", "min_amount_cents must be a positive integer");
    Validation::require(errs, body, "max_amount_cents");
    if (body.contains("max_amount_cents") && !body["max_amount_cents"].is_null() &&
        (!body["max_amount_cents"].is_number_integer() || body["max_amount_cents"].get<std::int64_t>() <= 0))
        errs.add("max_amount_cents", "invalid", "max_amount_cents must be a positive integer");
    Validation::int_range(errs, body, "credits_per_unit", 1, Billing::kMaxCreditsPerUnit);
    Validation::int_range(errs, body, "min_amount_cents", 1, Billing::kMaxAmountCents);
    Validation::int_range(errs, body, "max_amount_cents", 1, Billing::kMaxAmountCents);
    // The cross-field bound only makes sense once both individual fields
    // already checked out — deliberately gated on !errs.any(), unlike the
    // independent per-field checks above.
    if (!errs.any() && body["max_amount_cents"].get<std::int64_t>() < body["min_amount_cents"].get<std::int64_t>())
        errs.add("max_amount_cents", "below_min", "max_amount_cents must be >= min_amount_cents");
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }

    const auto credits_per_unit = body["credits_per_unit"].get<std::int64_t>();
    const auto min_amount_cents = body["min_amount_cents"].get<std::int64_t>();
    const auto max_amount_cents = body["max_amount_cents"].get<std::int64_t>();

    with_repo_errors(callback, "admin billing updateSettings", [&] {
        Repositories::BillingSettingsRepository repo;
        const auto before = repo.get(/*from_primary=*/true);
        auto updated = repo.update(credits_per_unit, min_amount_cents, max_amount_cents);
        Security::Audit::record(actor_of(req),
                                "billing.settings.update",
                                "billing_settings",
                                "1",
                                {{"credits_per_unit", credits_per_unit},
                                 {"min_amount_cents", min_amount_cents},
                                 {"max_amount_cents", max_amount_cents},
                                 {"before", json(before)},
                                 {"after", json(updated)}});
        callback(Response::ok({{"data", json(updated)}}));
    });
}

void AdminBillingController::adjustWallet(const HttpRequestPtr& req,
                                          std::function<void(const HttpResponsePtr&)>&& callback,
                                          const std::string& id) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);
    if (!require_valid_uuid(id, callback))
        return;
    json body;
    if (!Validation::parse_body(req, body, callback))
        return;

    Validation::Errors errs;
    Validation::require(errs, body, "delta_credits");
    if (!errs.any() && !body["delta_credits"].is_number_integer())
        errs.add("delta_credits", "not_integer", "delta_credits must be an integer");
    else if (!errs.any() && body["delta_credits"].is_number_unsigned() &&
             body["delta_credits"].get<std::uint64_t>() > static_cast<std::uint64_t>(Billing::kMaxCredits))
        errs.add("delta_credits", "above_max", "max " + std::to_string(Billing::kMaxCredits));
    else
        Validation::int_range(errs, body, "delta_credits", -Billing::kMaxCredits, Billing::kMaxCredits);
    Validation::require(errs, body, "note");
    Validation::string_length(errs, body, "note", 1, 2000);
    if (errs.any()) {
        callback(Validation::response_400(errs));
        return;
    }
    const std::int64_t delta_credits = body["delta_credits"].get<std::int64_t>();
    const std::string note = body["note"].get<std::string>();
    // Optional, default false — silently ignored if present but not a
    // JSON boolean, same convention as updatePackage's `active` field.
    const bool notify = body.contains("notify") && body["notify"].is_boolean() && body["notify"].get<bool>();
    const std::string admin_id = actor_of(req);

    // Set ONLY when notify was requested AND Billing::adjust + the audit
    // write both already succeeded — consumed by the after_fn below.
    // Every earlier error path (validation, malformed id, ZeroAdjustment,
    // InsufficientBalance, unknown user/admin) leaves this unset, so
    // nothing dispatches for those.
    std::optional<Billing::CreditResult> adjust_result;

    with_repo_errors(
        callback,
        "admin billing adjustWallet",
        [&] {
            auto result = Billing::adjust(id, delta_credits, note, admin_id);
            Security::Audit::record(
                admin_id, "billing.wallet.adjust", "user", id, {{"delta_credits", delta_credits}, {"note", note}});
            callback(Response::ok({{"data", {{"balance", result.balance}, {"credited", result.credited}}}}));
            if (notify)
                adjust_result = result;
        },
        [&] {
            if (!adjust_result)
                return;
            auto user = load_user_for_adjustment_email(id);
            if (user)
                Email::BillingEmails::adjustment(*user, delta_credits, note, adjust_result->balance);
        });
}

void AdminBillingController::metrics(const HttpRequestPtr& req,
                                     std::function<void(const HttpResponsePtr&)>&& callback) {
    if (!require_billing_enabled(callback))
        return;
    API_REQUIRE_ADMIN(req, callback);

    // ?period=day|week|month, default week; anything else -> 400. Checked
    // against a fixed allow-list BEFORE it ever reaches
    // MetricsWindow::for_period / SQL, same discipline as every other
    // client-supplied enum in this controller (e.g. listPayments' ?status=).
    std::string period = req->getParameter("period");
    if (period.empty())
        period = "week";
    if (period != "day" && period != "week" && period != "month") {
        callback(ErrorResponse::bad_request("invalid_period", "period must be one of: day, week, month"));
        return;
    }

    with_repo_errors(callback, "admin billing metrics", [&] {
        // Current rate, same source BillingController::billing_limits() uses
        // for a live top-up — see BillingMetricsRepository.hpp's file doc
        // comment for why the repository itself doesn't read billing_settings.
        Repositories::BillingSettingsRepository settings_repo;
        const auto settings = settings_repo.get();

        Repositories::BillingMetricsRepository repo;
        const auto w = Repositories::MetricsWindow::for_period(period);
        auto m = repo.get(w, settings.credits_per_unit);

        json series = json::array();
        for (const auto& pt : m.series)
            series.push_back({{"bucket_start", pt.bucket_start},
                              {"revenue_cents", pt.revenue_cents},
                              {"payments_count", pt.payments_count}});

        json top_packages = json::array();
        for (const auto& tp : m.top_packages)
            top_packages.push_back({{"package_id", tp.package_id},
                                    {"title", tp.title},
                                    {"revenue_cents", tp.revenue_cents},
                                    {"payments_count", tp.payments_count}});

        json top_users = json::array();
        for (const auto& tu : m.top_users)
            top_users.push_back({{"user_id", tu.user_id},
                                 {"email", tu.email},
                                 {"topup_credits", tu.topup_credits},
                                 {"revenue_cents", tu.revenue_cents}});

        json refunds_skipped = json::array();
        for (const auto& sr : m.refunds_skipped)
            refunds_skipped.push_back({{"provider_refund_id", sr.provider_refund_id},
                                       {"payment_id", sr.payment_id},
                                       {"user_id", sr.user_id},
                                       {"email", sr.email},
                                       {"amount_cents", sr.amount_cents},
                                       {"outcome", sr.outcome},
                                       {"created_at", sr.created_at}});

        json conversion = {
            {"created", m.conversion_created}, {"captured", m.conversion_captured}, {"rate", m.conversion_rate}};

        json data = {{"period", period},
                     {"revenue_cents", m.revenue_cents},
                     {"payments_count", m.payments_count},
                     {"avg_payment_cents", m.avg_payment_cents},
                     {"conversion", conversion},
                     {"refunds_cents", m.refunds_cents},
                     {"refunds_count", m.refunds_count},
                     {"refunds_skipped_cents", m.refunds_skipped_cents},
                     {"refunds_skipped_count", m.refunds_skipped_count},
                     {"refunds_skipped", refunds_skipped},
                     {"outstanding_credits", m.outstanding_credits},
                     {"outstanding_value_cents", m.outstanding_value_cents},
                     {"series", series},
                     {"top_packages", top_packages},
                     {"top_users", top_users}};

        callback(Response::ok({{"data", data}}));
    });
}

bool AdminBillingController::require_billing_enabled(const std::function<void(const HttpResponsePtr&)>& callback) {
    if (Core::billing_enabled())
        return true;
    callback(ErrorResponse::not_found("billing"));
    return false;
}

std::string AdminBillingController::actor_of(const HttpRequestPtr& req) {
    auto p = Security::Auth::principal_of(req);
    return p ? p->subject : std::string{};
}

std::optional<Domain::User> AdminBillingController::load_user_for_adjustment_email(const std::string& user_id) {
    try {
        Repositories::UserRepository users;
        auto u = users.find(user_id);
        if (!u)
            spdlog::warn("billing email: user {} not found for adjustment notice — skipping", user_id);
        return u;
    } catch (const std::exception& e) {
        spdlog::warn("billing email: failed to load user {} for adjustment notice: {}", user_id, e.what());
        return std::nullopt;
    }
}

}  // namespace Api
