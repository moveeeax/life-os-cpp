/**
 * @file TelegramActions.cpp
 * @brief The Telegram write tools, tg_send and tg_edit (agent/ActionKit.hpp).
 */

#include <string>
#include <utility>
#include <variant>

#include "agent/ActionKit.hpp"
#include "agent/SiteApi.hpp"
#include "telegram/Gateway.hpp"
#include "utils/Crypto.hpp"

namespace Agent::Actions::detail {

namespace {

const json kId = {{"type", "string"}, {"description", "An id as tg_chats and tg_history give it"}};

/// A Telegram chat or message id argument (digits, a minus for a group); empty when it is not one.
std::string tg_id(const json& args, const char* key) {
    return ToolArgs::tg_id(args, key, /*trim=*/true);
}

/// The title of the person's chat, or a refusal when Telegram is not theirs or the chat is unknown.
std::variant<std::string, Proposed> chat_title(const Ctx& c, const std::string& chat) {
    const auto data =
        SiteApi::read_data(c.transport, c.api_url, c.token, "/api/v1/telegram/chats/" + chat, json::array());
    if (!data || !data->is_object()) {
        return Proposed{refusal("not_available", "this chat is not known or Telegram is off for the person")};
    }
    const std::string title = data->value("title", std::string());
    return title.empty() ? std::string("a chat") : title;
}

}  // namespace

json tg_send_tool() {
    return tool("tg_send",
                "Propose a Telegram message from the person to a chat, as a reply to a message if given. Nothing is "
                "sent until they confirm it; they may change the text first.",
                object({{"chat", kId},
                        {"text", {{"type", "string"}, {"description", "The message, in the person's voice"}}},
                        {"reply_to", kId}},
                       json::array({"chat", "text"})));
}

json tg_edit_tool() {
    return tool(
        "tg_edit",
        "Propose a new text for one of the person's own Telegram messages. Nothing changes until they "
        "confirm it.",
        object({{"chat", kId}, {"message", kId}, {"text", {{"type", "string"}, {"description", "The new text"}}}},
               json::array({"chat", "message", "text"})));
}

Proposed tg_send(const Ctx& c, const json& args) {
    const std::string chat = tg_id(args, "chat");
    const std::string text = text_arg(args, "text");
    if (chat.empty()) {
        return refusal("invalid", "chat is the id from tg_chats");
    }
    // Bytes, as the Telegram route counts them: 4000 characters of Cyrillic passed here and failed there.
    if (text.empty() || text.size() > ::Telegram::kTextMaxBytes) {
        return refusal("invalid", "the message is 1 to 4096 bytes (about 2000 Cyrillic or 4000 Latin letters)");
    }
    auto title = chat_title(c, chat);
    if (auto* p = std::get_if<Proposed>(&title)) {
        return std::move(*p);
    }
    json body{{"text", text}, {"key", "ani-" + Utils::Crypto::random_hex(12)}};
    const std::string reply_to = tg_id(args, "reply_to");
    if (!reply_to.empty()) {
        body["reply_to"] = reply_to;
    }
    // The label is the summary before the text: a new text from the person keeps it (retext).
    const std::string label = "Send to " + cut(std::get<std::string>(title), 60) + ": ";
    return Proposal{"tg_send",
                    label + "«" + cut(text, 300) + "»",
                    json{{"method", "POST"},
                         {"path", "/api/v1/telegram/chats/" + chat + "/messages"},
                         {"body", body},
                         {"label", label}}};
}

Proposed tg_edit(const Ctx& c, const json& args) {
    const std::string chat = tg_id(args, "chat");
    const std::string message = tg_id(args, "message");
    const std::string text = text_arg(args, "text");
    if (chat.empty() || message.empty()) {
        return refusal("invalid", "chat and message are ids from tg_chats and tg_history");
    }
    // Bytes, as the Telegram route counts them: 4000 characters of Cyrillic passed here and failed there.
    if (text.empty() || text.size() > ::Telegram::kTextMaxBytes) {
        return refusal("invalid", "the message is 1 to 4096 bytes (about 2000 Cyrillic or 4000 Latin letters)");
    }
    auto title = chat_title(c, chat);
    if (auto* p = std::get_if<Proposed>(&title)) {
        return std::move(*p);
    }
    const std::string label = "Edit your message in " + cut(std::get<std::string>(title), 60) + " to ";
    return Proposal{"tg_edit",
                    label + "«" + cut(text, 300) + "»",
                    json{{"method", "PATCH"},
                         {"path", "/api/v1/telegram/chats/" + chat + "/messages/" + message},
                         {"body", {{"text", text}}},
                         {"label", label}}};
}

}  // namespace Agent::Actions::detail
