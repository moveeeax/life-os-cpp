/**
 * @file AniController.cpp
 * @brief Body for src/api/AniController.hpp.
 */

#include "api/AniController.hpp"

#include <exception>
#include <string>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "agent/Lifecycle.hpp"
#include "agent/Settings.hpp"
#include "api/Guards.hpp"
#include "jobs/Jobs.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;

/// The caller's agent as the page shows it; a user who never woke it reads as stopped.
json view(const std::string& owner) {
    Repositories::Agent::AgentRepository repo;
    const auto row = repo.get(owner);
    const auto settings = Agent::read_settings();
    json usage = repo.month_usage(owner);
    usage["budget_tokens"] = settings ? settings->monthly_tokens : 0;
    return json{{"state", row ? row->value("state", "stopped") : "stopped"},
                {"started_at", row ? (*row)["started_at"] : json()},
                {"last_active_at", row ? (*row)["last_active_at"] : json()},
                {"generation", row ? row->value("generation", 0) : 0},
                {"last_error", row ? (*row)["last_error"] : json()},
                {"usage", usage}};
}

HttpResponsePtr accepted(const json& body) {
    auto r = Response::ok(body);
    r->setStatusCode(k202Accepted);
    return r;
}

}  // namespace

#define ANI_AGENT_GUARD(req, callback, owner)                      \
    API_REQUIRE_MODULE(::Agent::enabled(), "ani_agent", callback); \
    API_REQUIRE_USER(req, callback, owner);                        \
    API_REFUSE_API_KEY(req, callback);                             \
    API_REQUIRE_ADMIN(req, callback)

void AniController::agent(const HttpRequestPtr& req, Callback&& callback) {
    ANI_AGENT_GUARD(req, callback, owner);
    try {
        callback(Response::ok(json{{"data", view(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.agent failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::wake(const HttpRequestPtr& req, Callback&& callback) {
    ANI_AGENT_GUARD(req, callback, owner);
    API_REQUIRE_JOBS_READY(callback);
    try {
        switch (::Agent::wake(owner)) {
            case ::Agent::Wake::Off:
                callback(ErrorResponse::service_unavailable("not_configured", "the agent is not configured"));
                return;
            case ::Agent::Wake::Busy:
                callback(ErrorResponse::service_unavailable("busy", "every agent slot is taken, try again later"));
                return;
            case ::Agent::Wake::Started:
            case ::Agent::Wake::AlreadyUp:
                callback(accepted(json{{"data", view(owner)}}));
                return;
        }
    } catch (const std::exception& e) {
        spdlog::error("ani.wake failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::stop(const HttpRequestPtr& req, Callback&& callback) {
    ANI_AGENT_GUARD(req, callback, owner);
    API_REQUIRE_JOBS_READY(callback);
    try {
        ::Agent::request_stop(owner);
        callback(accepted(json{{"data", view(owner)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.stop failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
