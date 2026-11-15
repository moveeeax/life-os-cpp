/**
 * @file TelegramTools.cpp
 * @brief Body for src/agent/TelegramTools.hpp.
 */

#include "agent/TelegramTools.hpp"

#include <exception>
#include <utility>

#include <spdlog/spdlog.h>

#include "agent/SiteApi.hpp"
#include "agent/ToolArgs.hpp"
#include "agent/Tools.hpp"
#include "repositories/UserRepository.hpp"
#include "repositories/telegram/LinkRepository.hpp"
#include "telegram/Gateway.hpp"

namespace Agent::TelegramTools {

namespace {

using json = nlohmann::json;

using ToolArgs::object;
using ToolArgs::text_arg;
using ToolArgs::tool;

const json kId = {{"type", "string"}, {"description", "An id as tg_chats, tg_history or tg_search give it"}};

/// An id argument (a string or a whole number); empty when it is not a Telegram id.
std::string id_arg(const json& args, const char* key) {
    return ToolArgs::tg_id(args, key);
}

std::string number_arg(const json& args, const char* key, int max) {
    const auto n = ToolArgs::int_arg(args, key, 1, max);
    return n ? std::to_string(*n) : std::string();
}

}  // namespace

bool available(const std::string& owner) {
    try {
        if (!::Telegram::settings().has_value() || !Repositories::Telegram::LinkRepository().of(owner).has_value()) {
            return false;
        }
        // The routes serve the linked user only while they are an administrator.
        const auto user = Repositories::UserRepository().find(owner);
        return user && user->is_admin();
    } catch (const std::exception& e) {
        spdlog::warn("ani agent: the Telegram link was not read: {}", e.what());
        return false;
    }
}

json definitions() {
    return json::array({
        tool("tg_chats",
             "The person's recent Telegram chats with their unread counts, latest first.",
             object({{"limit", {{"type", "integer"}, {"description", "How many, 1 to 50; 20 when left out"}}}})),
        tool("tg_history",
             "Messages of one Telegram chat, newest first. In a group a message has no author: only whether it "
             "is the person's own (outgoing).",
             object({{"chat", kId},
                     {"limit", {{"type", "integer"}, {"description", "1 to 30; 20 when left out"}}},
                     {"before", {{"type", "string"}, {"description", "Only messages older than this message id"}}}},
                    json::array({"chat"}))),
        tool("tg_search",
             "Search the person's Telegram messages by words, in all chats or in one.",
             object({{"query", {{"type", "string"}, {"description", "Words to find"}}}, {"chat", kId}},
                    json::array({"query"}))),
        tool("tg_react",
             "Put an emoji reaction on a Telegram message, or take it off. This is done at once, so react only "
             "when it fits what the person would do.",
             object({{"chat", kId},
                     {"message", kId},
                     {"emoji", {{"type", "string"}, {"description", "One emoji, like 👍 or ❤️"}}},
                     {"remove", {{"type", "boolean"}, {"description", "Take the reaction off"}}}},
                    json::array({"chat", "message", "emoji"}))),
        tool("tg_mark_read",
             "Mark Telegram messages of a chat as read. This is done at once.",
             object({{"chat", kId}, {"messages", {{"type", "array"}, {"items", kId}, {"description", "Message ids"}}}},
                    json::array({"chat", "messages"}))),
    });
}

bool is_telegram(const std::string& name) {
    return name == "tg_chats" || name == "tg_history" || name == "tg_search" || name == "tg_react" ||
           name == "tg_mark_read";
}

bool is_offered_in_hook(const std::string& name) {
    return name == "tg_chats" || name == "tg_history" || name == "tg_search";
}

json request_of(const std::string& name, const json& raw, std::string& refusal) {
    const json args = raw.is_object() ? raw : json::object();
    auto refuse = [&](const std::string& message) {
        refusal = ToolArgs::refusal("invalid", message);
        return json();
    };
    if (name == "tg_chats") {
        json q = json::array();
        if (const auto n = number_arg(args, "limit", 50); !n.empty()) {
            q.push_back(json::array({"limit", n}));
        }
        return json{{"method", "GET"}, {"path", "/api/v1/telegram/chats"}, {"query", q}};
    }
    const std::string chat = id_arg(args, "chat");
    if (name == "tg_search") {
        const std::string query = text_arg(args, "query");
        if (query.empty() || query.size() > 200) {
            return refuse("query is 1 to 200 characters");
        }
        json q = json::array({json::array({"q", query})});
        if (!chat.empty()) {
            q.push_back(json::array({"chat", chat}));
        }
        return json{{"method", "GET"}, {"path", "/api/v1/telegram/search"}, {"query", q}};
    }
    if (chat.empty()) {
        return refuse("chat is an id from tg_chats");
    }
    if (name == "tg_history") {
        json q = json::array();
        if (const auto n = number_arg(args, "limit", 30); !n.empty()) {
            q.push_back(json::array({"limit", n}));
        }
        if (const auto before = id_arg(args, "before"); !before.empty()) {
            q.push_back(json::array({"before", before}));
        }
        return json{{"method", "GET"}, {"path", "/api/v1/telegram/chats/" + chat + "/messages"}, {"query", q}};
    }
    if (name == "tg_react") {
        const std::string message = id_arg(args, "message");
        const std::string emoji = text_arg(args, "emoji");
        if (message.empty() || emoji.empty() || emoji.size() > 32) {
            return refuse("message is an id from tg_history and emoji one emoji");
        }
        return json{{"method", "POST"},
                    {"path", "/api/v1/telegram/chats/" + chat + "/messages/" + message + "/reactions"},
                    {"body", {{"emoji", emoji}, {"remove", args.value("remove", false)}}}};
    }
    if (name == "tg_mark_read") {
        json ids = json::array();
        for (const auto& m : args.value("messages", json::array())) {
            const json one = json{{"id", m}};
            const std::string id = id_arg(one, "id");
            if (id.empty()) {
                return refuse("messages are ids from tg_history");
            }
            ids.push_back(id);
        }
        if (ids.empty() || ids.size() > 100) {
            return refuse("messages are 1 to 100 ids from tg_history");
        }
        return json{
            {"method", "POST"}, {"path", "/api/v1/telegram/chats/" + chat + "/read"}, {"body", {{"message_ids", ids}}}};
    }
    refusal = ToolArgs::refusal("unknown_tool", "there is no tool " + name);
    return json();
}

Gateway::ToolRunner runner(Net::Http::Transport& transport,
                           const std::string& api_url,
                           const std::string& access_token,
                           Gateway::ToolRunner rest) {
    return [&transport, api_url, access_token, rest = std::move(rest)](const std::string& name,
                                                                       const json& args) -> std::string {
        if (!is_telegram(name)) {
            return rest(name, args);
        }
        std::string refusal;
        const json req = request_of(name, args, refusal);
        if (!refusal.empty()) {
            return refusal;
        }
        return SiteApi::tool_result(transport, api_url, access_token, req, /*keep_ids=*/true, name);
    };
}

}  // namespace Agent::TelegramTools
