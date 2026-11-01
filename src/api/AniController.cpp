/**
 * @file AniController.cpp
 * @brief Body for src/api/AniController.hpp.
 */

#include "api/AniController.hpp"

#include <exception>
#include <memory>
#include <string>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "agent/Actions.hpp"
#include "agent/Lifecycle.hpp"
#include "agent/Settings.hpp"
#include "agent/Turn.hpp"
#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "api/RequestUtils.hpp"
#include "cache/Cache.hpp"
#include "jobs/Jobs.hpp"
#include "net/Http.hpp"
#include "now/Voice.hpp"
#include "repositories/Zones.hpp"
#include "repositories/agent/ActionRepository.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "repositories/agent/TurnRepository.hpp"
#include "telegram/Gateway.hpp"
#include "utils/BlockingPool.hpp"
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

/// Characters, not bytes: the limit of a message is what the person typed.
std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
        n += (c & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

HttpResponsePtr accepted(const json& body) {
    auto r = Response::ok(body);
    r->setStatusCode(k202Accepted);
    return r;
}

/// The refusal of an answer that did not take; nullptr when it did.
HttpResponsePtr unanswered(Repositories::Agent::ActionRepository::Answer a) {
    using Answer = Repositories::Agent::ActionRepository::Answer;
    switch (a) {
        case Answer::Done:
            return nullptr;
        case Answer::Missing:
            return ErrorResponse::not_found("action_not_found");
        case Answer::Expired:
            return ErrorResponse::conflict("action_expired", "this proposal is too old: ask her again");
        case Answer::NotPending:
            break;
    }
    return ErrorResponse::conflict("action_answered", "this proposal was confirmed or cancelled already");
}

}  // namespace

#define ANI_AGENT_GUARD(req, callback, owner)                      \
    API_REQUIRE_MODULE(::Agent::enabled(), "ani_agent", callback); \
    API_REQUIRE_USER(req, callback, owner);                        \
    API_REFUSE_API_KEY(req, callback)

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

void AniController::message(const HttpRequestPtr& req, Callback&& callback) {
    ANI_AGENT_GUARD(req, callback, owner);
    API_REQUIRE_JOBS_READY(callback);
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto text_it = body.find("text");
    const std::string text = text_it != body.end() && text_it->is_string() ? text_it->get<std::string>() : "";
    const std::size_t length = utf8_length(text);
    if (text.find_first_not_of(" \t\r\n") == std::string::npos || length > ::Agent::kTurnTextMax) {
        callback(ErrorResponse::bad_request("invalid_text",
                                            "text is 1 to " + std::to_string(::Agent::kTurnTextMax) + " characters"));
        return;
    }
    const auto tz_it = body.find("tz");
    const std::string tz = tz_it != body.end() && tz_it->is_string() ? tz_it->get<std::string>() : "UTC";
    if (!Repositories::Zones::is_zone(tz)) {
        callback(ErrorResponse::bad_request("invalid_tz", "tz must be an IANA zone like Asia/Almaty"));
        return;
    }
    try {
        Repositories::Agent::TurnRepository turns;
        if (turns.count_since(owner, 3600) >= ::Agent::kTurnsPerHour) {
            callback(ErrorResponse::too_many_requests(600));
            return;
        }
        auto sent = ::Agent::send(owner, text, tz);
        switch (sent.status) {
            case ::Agent::Send::Queued:
                callback(accepted(json{{"data", sent.turn}}));
                return;
            case ::Agent::Send::Open:
                callback(ErrorResponse::conflict("turn_open", "she is still answering the last message"));
                return;
            case ::Agent::Send::Busy:
                callback(ErrorResponse::service_unavailable("busy", "every agent slot is taken, try again later"));
                return;
            case ::Agent::Send::Budget:
                callback(ErrorResponse::unprocessable("budget", "this month's tokens are spent"));
                return;
            case ::Agent::Send::Off:
                callback(ErrorResponse::service_unavailable("not_configured", "the agent is not configured"));
                return;
        }
    } catch (const std::exception& e) {
        spdlog::error("ani.message failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::turns(const HttpRequestPtr& req, Callback&& callback) {
    ANI_AGENT_GUARD(req, callback, owner);
    const std::string before = req->getParameter("before");
    if (!before.empty() && !is_valid_uuid(before)) {
        callback(ErrorResponse::bad_request("invalid_query", "before must be a turn id"));
        return;
    }
    const int limit = clamp_int(req->getParameter("limit"), 30, 1, 100);
    try {
        callback(Response::ok(json{{"data", Repositories::Agent::TurnRepository().list(owner, before, limit)}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.turns failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::turn(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_AGENT_GUARD(req, callback, owner);
    if (!is_valid_uuid(id)) {
        callback(ErrorResponse::not_found("turn_not_found"));
        return;
    }
    try {
        const auto t = Repositories::Agent::TurnRepository().get(owner, id);
        if (!t) {
            callback(ErrorResponse::not_found("turn_not_found"));
            return;
        }
        callback(Response::ok(json{{"data", *t}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.turn failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::speech(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_AGENT_GUARD(req, callback, owner);
    if (!is_valid_uuid(id)) {
        callback(ErrorResponse::not_found("turn_not_found"));
        return;
    }
    const auto settings = ::Now::Voice::read_settings();
    if (!settings) {
        callback(ErrorResponse::service_unavailable("not_configured", "ANI_VOICE_API_KEY is not set"));
        return;
    }
    std::string reply;
    try {
        const auto t = Repositories::Agent::TurnRepository().get(owner, id);
        if (!t || t->value("status", "") != "done" || !(*t)["reply"].is_string()) {
            callback(ErrorResponse::not_found("turn_not_found"));
            return;
        }
        reply = (*t)["reply"].get<std::string>();
    } catch (const std::exception& e) {
        spdlog::error("ani.speech failed: {}", e.what());
        callback(ErrorResponse::internal_error());
        return;
    }
    // Every reply read aloud is paid for from the voice key.
    if (Cache::is_initialized()) {
        try {
            const std::string rate_key = "ani:speech:rate:" + owner;
            const long long n = Cache::get().incr(rate_key);
            if (n == 1) {
                Cache::get().expire(rate_key, ::Now::Voice::kWindowSeconds);
            }
            if (n > ::Now::Voice::kMaxSpeechPerWindow) {
                callback(ErrorResponse::too_many_requests(static_cast<int>(::Now::Voice::kWindowSeconds)));
                return;
            }
        } catch (const std::exception& e) {
            spdlog::warn("ani.speech: the rate counter failed: {}", e.what());
        }
    }
    // A few seconds at OpenAI: the call runs on the blocking pool.
    auto respond = std::make_shared<Callback>(std::move(callback));
    const bool queued = Utils::BlockingPool::shared().try_submit([respond, s = *settings, text = std::move(reply)] {
        const Callback& answer = *respond;
        try {
            auto r = HttpResponse::newHttpResponse();
            r->setContentTypeString("audio/mpeg");
            r->addHeader("Cache-Control", "private, max-age=86400");
            r->setBody(::Now::Voice::speak(Net::Http::transport(), s, text));
            answer(r);
        } catch (const ::Now::Voice::Unavailable& e) {
            spdlog::warn("ani.speech: no audio: {}", e.what());
            answer(ErrorResponse::service_unavailable("upstream_unavailable", "the voice service is not answering"));
        } catch (const std::exception& e) {
            spdlog::error("ani.speech failed: {}", e.what());
            answer(ErrorResponse::internal_error());
        }
    });
    if (!queued) {
        (*respond)(ErrorResponse::service_unavailable("busy", "the server is busy, try again"));
    }
}

void AniController::removeTurn(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_AGENT_GUARD(req, callback, owner);
    if (!is_valid_uuid(id)) {
        callback(ErrorResponse::not_found("turn_not_found"));
        return;
    }
    try {
        using Removed = Repositories::Agent::TurnRepository::Removed;
        switch (Repositories::Agent::TurnRepository().remove(owner, id)) {
            case Removed::Done:
                no_content(callback);
                return;
            case Removed::Open:
                callback(ErrorResponse::conflict("turn_open", "she is still answering this message"));
                return;
            case Removed::Missing:
                callback(ErrorResponse::not_found("turn_not_found"));
                return;
        }
    } catch (const std::exception& e) {
        spdlog::error("ani.removeTurn failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::clearTurns(const HttpRequestPtr& req, Callback&& callback) {
    ANI_AGENT_GUARD(req, callback, owner);
    try {
        Repositories::Agent::TurnRepository turns;
        if (turns.has_open(owner)) {
            callback(ErrorResponse::conflict("turn_open", "she is still answering the last message"));
            return;
        }
        turns.clear(owner);
        Repositories::Agent::AgentRepository().next_chat_session(owner);
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("ani.clearTurns failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::confirmAction(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_AGENT_GUARD(req, callback, owner);
    API_REQUIRE_JOBS_READY(callback);
    if (!is_valid_uuid(id)) {
        callback(ErrorResponse::not_found("action_not_found"));
        return;
    }
    // A Telegram proposal may come with the person's own words for the message.
    std::string text;
    if (!req->body().empty()) {
        const auto body = json::parse(req->body(), nullptr, /*allow_exceptions=*/false);
        if (!body.is_object() || (body.contains("text") && !body["text"].is_string())) {
            callback(ErrorResponse::bad_request("invalid_body", "the body is {\"text\": \"...\"} or empty"));
            return;
        }
        text = body.value("text", std::string());
        // The Telegram route's own limit, in its unit: a longer text would be confirmed and then refused.
        if (body.contains("text") && (text.empty() || text.size() > ::Telegram::kTextMaxBytes)) {
            callback(ErrorResponse::bad_request("invalid_text", "text is 1 to 4096 bytes of UTF-8"));
            return;
        }
    }
    try {
        Repositories::Agent::ActionRepository actions;
        if (!text.empty() && !actions.retext(owner, id, text)) {
            callback(ErrorResponse::conflict("action_not_editable", "only a pending Telegram message takes a text"));
            return;
        }
        if (auto refused = unanswered(actions.answer(owner, id, /*confirm=*/true))) {
            callback(refused);
            return;
        }
        // Picking one of her drafted replies drops the other drafts to that chat.
        actions.cancel_sibling_drafts(owner, id);
        Jobs::get().submit(::Agent::Actions::kActionJob, json{{"action", id}});
        callback(accepted(json{{"data", actions.get(owner, id).value_or(json::object())}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.confirmAction failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void AniController::cancelAction(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    ANI_AGENT_GUARD(req, callback, owner);
    if (!is_valid_uuid(id)) {
        callback(ErrorResponse::not_found("action_not_found"));
        return;
    }
    try {
        Repositories::Agent::ActionRepository actions;
        if (auto refused = unanswered(actions.answer(owner, id, /*confirm=*/false))) {
            callback(refused);
            return;
        }
        callback(Response::ok(json{{"data", actions.get(owner, id).value_or(json::object())}}));
    } catch (const std::exception& e) {
        spdlog::error("ani.cancelAction failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

}  // namespace Api
