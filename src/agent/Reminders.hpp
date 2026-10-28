/**
 * @file Reminders.hpp
 * @brief Ani's recurring reminders at work: every minute the job
 *        ani_reminders_due turns the reminders that are due into remind
 *        items of her inbox (said on the open Now page, an hour to wait)
 *        and moves each on to its next time (ReminderRepository).
 */

#pragma once

#include <string>
#include <variant>

#include <nlohmann/json.hpp>

namespace Agent::Reminders {

/// A reminder as the API and the MCP take it, checked.
struct Input {
    std::string text;  // one spoken line, 1-200 characters
    std::string at;    // HH:MM, local
    int days = 0;      // Mon=1 .. Sun=64
    std::string tz;    // IANA
};

/// Checks {text, time: "HH:MM", days: [1..7] (Mon=1; default every day), tz?}
/// (@p fallback_tz when tz is absent); an error message when wrong.
std::variant<Input, std::string> parse(const nlohmann::json& body, const std::string& fallback_tz);

inline constexpr const char* kDueJob = "ani_reminders_due";
/// How long a reminder's line waits for the page.
inline constexpr int kLineTtl = 3600;

/// The job handler: returns {"status":"done","said":n}.
nlohmann::json process_due(const nlohmann::json& payload);

}  // namespace Agent::Reminders
