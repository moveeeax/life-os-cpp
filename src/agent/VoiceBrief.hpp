/**
 * @file VoiceBrief.hpp
 * @brief Ani's morning and evening line as a voice message to the owner's
 *        Saved Messages (the Telegram card's switch): due at 08:00 and 21:00
 *        on the person's clock, once a day each, spoken by her voice (OpenAI
 *        speech, Ogg Opus) and sent through the gateway. The words are built
 *        from counts of the day (brief_words), so they need no agent.
 */

#pragma once

#include <optional>
#include <string>

#include "net/Http.hpp"

namespace Agent::VoiceBrief {

/// What the line is built from: the day's tasks and daily habits.
struct Counts {
    int due_today = 0;
    int overdue = 0;
    int done_today = 0;
    int habits = 0;
    int habits_done = 0;
};

/// "morning" at 08:00-08:59, "evening" at 21:00-21:59 of @p local ("YYYY-MM-DD HH:MM, Dy"); nullopt otherwise.
std::optional<std::string> moment_of(const std::string& local);

/// The words of the @p kind line, in Russian, from her side (feminine, «ты»).
std::string brief_words(const std::string& kind, const Counts& c);

/// Sends the line due now for the owner of the Telegram link, once per day and kind; quiet when nothing is
/// due, the switch is off, or the voice or the gateway is not set up.
void send_due(Net::Http::Transport& transport, const std::string& owner, const std::string& me_id);

}  // namespace Agent::VoiceBrief
