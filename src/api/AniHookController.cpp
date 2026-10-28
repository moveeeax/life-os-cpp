/**
 * @file AniHookController.cpp
 * @brief Body for src/api/AniHookController.hpp.
 */

#include "api/AniHookController.hpp"

#include <exception>
#include <optional>
#include <string>
#include <variant>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"
#include "agent/Intake.hpp"
#include "agent/Presence.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "repositories/agent/HookRepository.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
namespace Hook = ::Agent::Hook;
using Repositories::Agent::InboxRepository;

HttpResponsePtr error(HttpStatusCode status, const std::string& code, const std::string& message, json extra = {}) {
    return ErrorResponse::make({status, code, message, extra.is_object() ? extra : json::object()});
}

void take(const HttpRequestPtr& req, AniHookController::Callback& callback, const std::string& token) {
    const std::string path = Hook::redact(req->path());
    std::optional<std::string> owner_opt;
    if (!token.empty()) {
        owner_opt = Repositories::Agent::HookRepository().owner_of(token);
    }
    if (!owner_opt) {
        callback(ErrorResponse::not_found("not_found"));
        return;
    }
    const std::string& owner = *owner_opt;
    if (!::Agent::Intake::within_rate(owner)) {
        callback(error(k429TooManyRequests,
                       "rate_limited",
                       "at most 120 calls an hour",
                       json{{"retry_after_sec", Hook::kWindowSeconds}}));
        return;
    }
    auto parsed = Hook::parse(req->body(), req->getHeader("Content-Type"));
    if (std::holds_alternative<Hook::Error>(parsed)) {
        const auto& e = std::get<Hook::Error>(parsed);
        spdlog::info("ani.hook {}: refused {} ({})", path, e.status, e.code);
        callback(error(static_cast<HttpStatusCode>(e.status), e.code, e.message));
        return;
    }
    try {
        auto got = ::Agent::Intake::accept(owner, std::get<Hook::Item>(std::move(parsed)), "webhook");
        if (const auto* r = std::get_if<::Agent::Intake::Refused>(&got)) {
            callback(error(static_cast<HttpStatusCode>(r->status), r->code, r->message, r->extra));
            return;
        }
        auto r = Response::ok(json{{"data", {{"id", std::get<::Agent::Intake::Accepted>(got).id}}}});
        r->setStatusCode(k202Accepted);
        callback(r);
    } catch (const std::exception& e) {
        spdlog::error("ani.hook {} failed: {}", path, e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace

void AniHookController::ingest(const HttpRequestPtr& req, Callback&& callback, const std::string& token) {
    take(req, callback, token);
}

void AniHookController::ingestHeader(const HttpRequestPtr& req, Callback&& callback) {
    take(req, callback, req->getHeader("X-Ani-Hook-Token"));
}

void AniHookController::claim(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    try {
        ::Agent::Presence::mark(owner);
        auto r = Response::ok(json{{"data", InboxRepository().claim(owner, kClaimMax)}});
        r->addHeader("Cache-Control", "no-store");
        callback(r);
    } catch (const std::exception& e) {
        spdlog::error("ani.inbox claim failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniHookController::count(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    try {
        ::Agent::Presence::mark(owner);
        auto r = Response::ok(json{{"data", {{"waiting", InboxRepository().waiting(owner)}}}});
        r->addHeader("Cache-Control", "no-store");
        callback(r);
    } catch (const std::exception& e) {
        spdlog::error("ani.inbox count failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
