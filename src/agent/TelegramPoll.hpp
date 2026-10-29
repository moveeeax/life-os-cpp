/**
 * @file TelegramPoll.hpp
 * @brief The minutely look at the owner's Telegram (job telegram_poll, run by
 *        the ani worker, queued by the API every minute). It lists the chats
 *        with unread messages, reads each one down to its cursor and keeps the
 *        new incoming messages addressed to the owner: any in a private chat,
 *        and those in a group that name their @username. Together they become
 *        one Ani event. The gateway's messages carry no sender, entities or
 *        reply_to, so a reply in a group is not seen (owner's call,
 *        2026-10-29). The first poll after linking only sets the cursors.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Agent::TelegramPoll {

inline constexpr const char* kPollJob = "telegram_poll";
/// Chats read per poll, messages per chat, messages named in one event.
inline constexpr int kChatsPerPoll = 30;
inline constexpr int kMessagesPerChat = 50;
inline constexpr int kMessagesPerEvent = 10;
/// A chat seen for the first time after the link: only its messages this recent count.
inline constexpr long long kNewChatSeconds = 600;

struct Incoming {
    std::string chat_id;
    std::string chat_title;
    long long id = 0;
    long long date = 0;
    /// "dm" or "mention".
    std::string kind;
    std::string text;
};

/// Whether @p text names @p username as "@username" (any case, not inside a longer word).
bool mentions(const std::string& text, const std::string& username);

/// The new incoming messages of @p chat (a gateway Chat) among @p messages
/// (newest first) that are past @p cursor and addressed to the owner; an
/// unknown chat (@p cursor 0) keeps only those since @p since (unixtime).
std::vector<Incoming> addressed(const nlohmann::json& chat,
                                const nlohmann::json& messages,
                                long long cursor,
                                const std::string& username,
                                long long since);

/// A private chat waits for the owner's answer when its newest message is
/// incoming and older than a day: the unix time of that message, else 0.
long long waiting_since(const nlohmann::json& chat, const nlohmann::json& newest, long long now);

/// The event's text: up to kMessagesPerEvent lines "<chat>: <text>" (each
/// cut to 300 characters), "and N more" for the rest; at most 2000 characters.
std::string event_text(const std::vector<Incoming>& all);

/// The job handler (registered in agent/jobs/Register.cpp).
nlohmann::json process_poll(const nlohmann::json& payload);

}  // namespace Agent::TelegramPoll
