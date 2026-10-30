/**
 * @file TelegramController.cpp
 * @brief Body for src/api/TelegramController.hpp.
 */

#include "api/TelegramController.hpp"

#include <algorithm>
#include <ctime>
#include <exception>
#include <optional>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "net/Http.hpp"
#include "repositories/telegram/LinkRepository.hpp"
#include "security/Audit.hpp"
#include "telegram/Gateway.hpp"
#include "utils/ErrorResponse.hpp"

namespace Api {

namespace {

using json = nlohmann::json;
using Repositories::Telegram::LinkRepository;

/// A string field of a body, "" when it is missing or not a string.
std::string str_of(const json& body, const char* key) {
    const auto it = body.find(key);
    return it != body.end() && it->is_string() ? it->get<std::string>() : std::string();
}

constexpr std::size_t kTextMax = 4096;

/// The gateway settings, or a 404 for a site without Telegram.
std::optional<Telegram::Settings> configured(const TelegramController::Callback& callback) {
    auto s = Telegram::settings();
    if (!s) {
        callback(ErrorResponse::not_found("telegram_not_configured"));
    }
    return s;
}

/// The settings when @p owner is the linked user; otherwise 404.
std::optional<Telegram::Settings> served(const std::string& owner, const TelegramController::Callback& callback) {
    auto s = configured(callback);
    if (!s) {
        return std::nullopt;
    }
    if (!LinkRepository().of(owner)) {
        callback(ErrorResponse::not_found("telegram_not_linked"));
        return std::nullopt;
    }
    return s;
}

/// The gateway's refusal as ours: no answer 502, 404 and 429 as they are.
drogon::HttpResponsePtr refusal(const Telegram::Reply& r) {
    if (r.status == 404) {
        return ErrorResponse::not_found("telegram_not_found");
    }
    if (r.status == 429) {
        return ErrorResponse::too_many_requests(30);
    }
    std::string code = "telegram_unavailable";
    if (r.status != 0 && r.body.is_object()) {
        const json err = r.body.value("error", json());
        if (err.is_object() && err.value("code", json()).is_string()) {
            code = "telegram_" + err["code"].get<std::string>();
        } else if (err.is_string()) {
            code = "telegram_" + err.get<std::string>();
        }
    }
    return ErrorResponse::make({drogon::k502BadGateway, code.substr(0, 60), "", json{{"status", r.status}}});
}

std::string iso_of(long long unix_seconds) {
    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

json chat_view(const json& c) {
    return json{{"id", c.value("id", std::string())},
                {"title", c.value("title", std::string())},
                {"type", c.value("type", std::string())},
                {"unread_count", c.value("unread_count", 0)}};
}

json message_view(const json& m) {
    const json content = m.value("content", json::object());
    return json{{"id", m.value("id", std::string())},
                {"at", iso_of(m.value("date", 0LL))},
                {"outgoing", m.value("is_outgoing", false)},
                {"kind", content.is_object() ? content.value("type", std::string("text")) : std::string("text")},
                {"text", Telegram::text_of(m)}};
}

json list_of(const Telegram::Reply& r) {
    const json d = r.data();
    return d.is_array() ? d : json::array();
}

int int_param(const HttpRequestPtr& req, const char* key, int fallback, int max) {
    const std::string v = req->getParameter(key);
    if (v.empty()) {
        return fallback;
    }
    try {
        return std::clamp(std::stoi(v), 1, max);
    } catch (const std::exception&) {
        return fallback;
    }
}

/// A text of 1 to 4096 bytes from @p body[key], or a 400.
std::optional<std::string> text_field(const json& body, const TelegramController::Callback& callback) {
    const auto t = body.find("text");
    if (t == body.end() || !t->is_string() || t->get<std::string>().empty() ||
        t->get<std::string>().size() > kTextMax) {
        callback(ErrorResponse::bad_request("invalid_text", "text is 1 to 4096 characters"));
        return std::nullopt;
    }
    return t->get<std::string>();
}

bool ids_ok(const std::string& id, const TelegramController::Callback& callback) {
    if (!Telegram::valid_id(id)) {
        callback(ErrorResponse::not_found("telegram_not_found"));
        return false;
    }
    return true;
}

}  // namespace

void TelegramController::status(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_ADMIN(req, callback);
    API_REQUIRE_USER(req, callback, owner);
    try {
        const bool on = Telegram::settings().has_value();
        LinkRepository links;
        const auto current = links.current();
        json data{{"configured", on},
                  {"linked", current && current->owner == owner},
                  {"elsewhere", current && current->owner != owner},
                  {"link", nullptr}};
        if (current && current->owner == owner) {
            data["link"] = links.status(owner).value_or(json(nullptr));
        }
        callback(Response::ok(json{{"data", data}}));
    } catch (const std::exception& e) {
        spdlog::error("telegram.status failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void TelegramController::link(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_ADMIN(req, callback);
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    const auto s = configured(callback);
    if (!s) {
        return;
    }
    try {
        const auto me = Telegram::call(Net::Http::transport(), *s, "GET", "/v1/me");
        const json user = me.data();
        if (!me.ok() || !user.is_object() || user.value("id", std::string()).empty()) {
            callback(refusal(me));
            return;
        }
        const std::string me_id = user.value("id", std::string());
        // /v1/me carries no username; the user card does (the poll spots @mentions by it).
        std::string username = user.value("username", std::string());
        if (username.empty() && Telegram::valid_id(me_id)) {
            const auto card = Telegram::call(Net::Http::transport(), *s, "GET", "/v1/users/" + me_id);
            if (card.ok() && card.data().is_object()) {
                username = card.data().value("username", std::string());
            }
        }
        LinkRepository links;
        if (!links.set(owner, me_id, username)) {
            callback(ErrorResponse::conflict("telegram_linked_elsewhere", "the account serves another user"));
            return;
        }
        Security::Audit::record(owner, "telegram.link", "telegram_link", owner);
        callback(Response::ok(json{{"data", links.status(owner).value_or(json::object())}}));
    } catch (const std::exception& e) {
        spdlog::error("telegram.link failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void TelegramController::unlink(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_ADMIN(req, callback);
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    try {
        if (!LinkRepository().remove(owner)) {
            callback(ErrorResponse::not_found("telegram_not_linked"));
            return;
        }
        Security::Audit::record(owner, "telegram.unlink", "telegram_link", owner);
        no_content(callback);
    } catch (const std::exception& e) {
        spdlog::error("telegram.unlink failed: {}", e.what());
        callback(ErrorResponse::internal_error());
    }
}

void TelegramController::chats(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s) {
        return;
    }
    const int limit = int_param(req, "limit", 20, 50);
    const auto r = Telegram::call(Net::Http::transport(), *s, "GET", "/v1/chats", {{"limit", std::to_string(limit)}});
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    json out = json::array();
    for (const auto& c : list_of(r)) {
        out.push_back(chat_view(c));
    }
    callback(Response::ok(json{{"data", out}}));
}

void TelegramController::chat(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s || !ids_ok(id, callback)) {
        return;
    }
    const auto r = Telegram::call(Net::Http::transport(), *s, "GET", "/v1/chats/" + id);
    if (!r.ok() || !r.data().is_object()) {
        callback(refusal(r));
        return;
    }
    callback(Response::ok(json{{"data", chat_view(r.data())}}));
}

void TelegramController::messages(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s || !ids_ok(id, callback)) {
        return;
    }
    Telegram::Query q{{"limit", std::to_string(int_param(req, "limit", 20, 30))}};
    const std::string before = req->getParameter("before");
    if (!before.empty()) {
        if (!Telegram::valid_id(before)) {
            callback(ErrorResponse::bad_request("invalid_before", "before is a message id"));
            return;
        }
        q.emplace_back("from_id", before);
    }
    const auto r = Telegram::call(Net::Http::transport(), *s, "GET", "/v1/chats/" + id + "/messages", q);
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    json out = json::array();
    for (const auto& m : list_of(r)) {
        out.push_back(message_view(m));
    }
    callback(Response::ok(json{{"data", out}}));
}

void TelegramController::send(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s || !ids_ok(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto text = text_field(body, callback);
    if (!text) {
        return;
    }
    json out{{"text", *text}};
    const std::string reply_to = body.value("reply_to", std::string());
    if (!reply_to.empty()) {
        if (!Telegram::valid_id(reply_to)) {
            callback(ErrorResponse::bad_request("invalid_reply_to", "reply_to is a message id"));
            return;
        }
        out["reply_to_message_id"] = reply_to;
    }
    // A confirmed proposal carries its own key, so a retried job sends once.
    const std::string key = body.value("key", std::string()).substr(0, 80);
    const auto r = Telegram::call(Net::Http::transport(), *s, "POST", "/v1/chats/" + id + "/messages", {}, out, key);
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    Security::Audit::record(owner, "telegram.send", "telegram_chat", id);
    callback(Response::ok(json{{"data", json{{"sent", true}}}}));
}

void TelegramController::edit(const HttpRequestPtr& req,
                              Callback&& callback,
                              const std::string& id,
                              const std::string& mid) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s || !ids_ok(id, callback) || !ids_ok(mid, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto text = text_field(body, callback);
    if (!text) {
        return;
    }
    const auto r = Telegram::call(
        Net::Http::transport(), *s, "PATCH", "/v1/chats/" + id + "/messages/" + mid, {}, json{{"text", *text}});
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    Security::Audit::record(owner, "telegram.edit", "telegram_chat", id, json{{"message", mid}});
    callback(Response::ok(json{{"data", json{{"edited", true}}}}));
}

void TelegramController::react(const HttpRequestPtr& req,
                               Callback&& callback,
                               const std::string& id,
                               const std::string& mid) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s || !ids_ok(id, callback) || !ids_ok(mid, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const std::string emoji = body.value("emoji", std::string());
    if (emoji.empty() || emoji.size() > 32) {
        callback(ErrorResponse::bad_request("invalid_emoji", "emoji is one emoji"));
        return;
    }
    const bool remove = body.value("remove", false);
    const auto r = Telegram::call(Net::Http::transport(),
                                  *s,
                                  remove ? "DELETE" : "POST",
                                  "/v1/chats/" + id + "/messages/" + mid + "/reactions",
                                  {},
                                  json{{"emoji", emoji}});
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    Security::Audit::record(owner, "telegram.react", "telegram_chat", id, json{{"message", mid}, {"remove", remove}});
    callback(Response::ok(json{{"data", json{{"done", true}}}}));
}

void TelegramController::read(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s || !ids_ok(id, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const json ids = body.value("message_ids", json::array());
    if (!ids.is_array() || ids.empty() || ids.size() > 100) {
        callback(ErrorResponse::bad_request("invalid_message_ids", "message_ids is 1 to 100 message ids"));
        return;
    }
    json out = json::array();
    for (const auto& m : ids) {
        if (!m.is_string() || !Telegram::valid_id(m.get<std::string>())) {
            callback(ErrorResponse::bad_request("invalid_message_ids", "message_ids is 1 to 100 message ids"));
            return;
        }
        out.push_back(m);
    }
    const auto r = Telegram::call(
        Net::Http::transport(), *s, "POST", "/v1/chats/" + id + "/messages/read", {}, json{{"message_ids", out}});
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    Security::Audit::record(owner, "telegram.read", "telegram_chat", id, json{{"messages", out.size()}});
    callback(Response::ok(json{{"data", json{{"done", true}}}}));
}

void TelegramController::search(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s) {
        return;
    }
    const std::string q = req->getParameter("q");
    if (q.empty() || q.size() > 200) {
        callback(ErrorResponse::bad_request("invalid_q", "q is 1 to 200 characters"));
        return;
    }
    const std::string chat = req->getParameter("chat");
    if (!chat.empty() && !Telegram::valid_id(chat)) {
        callback(ErrorResponse::bad_request("invalid_chat", "chat is a chat id"));
        return;
    }
    const std::string limit = std::to_string(int_param(req, "limit", 10, 20));
    const auto r =
        chat.empty()
            ? Telegram::call(Net::Http::transport(), *s, "GET", "/v1/search", {{"q", q}, {"limit", limit}})
            : Telegram::call(
                  Net::Http::transport(), *s, "GET", "/v1/chats/" + chat + "/search", {{"q", q}, {"limit", limit}});
    if (!r.ok()) {
        callback(refusal(r));
        return;
    }
    json out = json::array();
    for (const auto& m : list_of(r)) {
        json v = message_view(m);
        v["chat"] = m.value("chat_id", std::string());
        out.push_back(std::move(v));
    }
    callback(Response::ok(json{{"data", out}}));
}

void TelegramController::waiting(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    if (!served(owner, callback)) {
        return;
    }
    callback(Response::ok(json{{"data", LinkRepository().waiting(owner)}}));
}

void TelegramController::quietList(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    if (!served(owner, callback)) {
        return;
    }
    json out = json::array();
    for (const auto& [chat, title] : LinkRepository().quiet(owner)) {
        out.push_back({{"chat", chat}, {"title", title}});
    }
    callback(Response::ok(json{{"data", out}}));
}

void TelegramController::quietOn(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!served(owner, callback) || !ids_ok(id, callback)) {
        return;
    }
    json body = json::object();
    if (!req->body().empty() && !parse_object(req, body, callback)) {
        return;
    }
    LinkRepository().set_quiet(owner, id, str_of(body, "title"), true);
    no_content(callback);
}

void TelegramController::quietOff(const HttpRequestPtr& req, Callback&& callback, const std::string& id) {
    API_REQUIRE_USER(req, callback, owner);
    if (!served(owner, callback) || !ids_ok(id, callback)) {
        return;
    }
    LinkRepository().set_quiet(owner, id, "", false);
    no_content(callback);
}

void TelegramController::settings(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    API_REFUSE_API_KEY(req, callback);
    if (!served(owner, callback)) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto on = body.find("review_to_self");
    if (on == body.end() || !on->is_boolean()) {
        callback(ErrorResponse::bad_request("invalid_review_to_self", "review_to_self is true or false"));
        return;
    }
    LinkRepository links;
    links.set_review_to_self(owner, on->get<bool>());
    callback(Response::ok(json{{"data", links.status(owner).value_or(json::object())}}));
}

void TelegramController::toSelf(const HttpRequestPtr& req, Callback&& callback) {
    API_REQUIRE_USER(req, callback, owner);
    const auto s = served(owner, callback);
    if (!s) {
        return;
    }
    json body;
    if (!parse_object(req, body, callback)) {
        return;
    }
    const auto text = text_field(body, callback);
    if (!text) {
        return;
    }
    LinkRepository links;
    const auto link = links.of(owner);
    // The weekly review goes once per ISO week and only with the switch on.
    const std::string week = str_of(body, "week").substr(0, 10);
    if (!week.empty() && !links.claim_review_week(owner, week)) {
        callback(ErrorResponse::conflict("review_not_sent", "the switch is off, or this week's review went already"));
        return;
    }
    // Saved Messages is the chat with oneself: its id is the account's own.
    const auto r = Telegram::call(
        Net::Http::transport(), *s, "POST", "/v1/chats/" + link->me_id + "/messages", {}, json{{"text", *text}});
    if (!r.ok()) {
        if (!week.empty()) {
            links.release_review_week(owner, week);
        }
        callback(refusal(r));
        return;
    }
    Security::Audit::record(owner, "telegram.send", "telegram_chat", link->me_id, json{{"self", true}});
    callback(Response::ok(json{{"data", json{{"sent", true}}}}));
}

}  // namespace Api
