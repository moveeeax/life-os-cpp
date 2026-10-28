/**
 * @file Reminders.cpp
 * @brief Body for src/agent/Reminders.hpp.
 */

#include "agent/Reminders.hpp"

#include <regex>

#include "now/Voice.hpp"
#include "repositories/Zones.hpp"
#include "repositories/agent/InboxRepository.hpp"
#include "repositories/agent/ReminderRepository.hpp"
#include "security/Audit.hpp"

namespace Agent::Reminders {

std::variant<Input, std::string> parse(const nlohmann::json& body, const std::string& fallback_tz) {
    if (!body.is_object()) {
        return std::string("the body is a JSON object");
    }
    Input in;
    const auto text = body.find("text");
    if (text == body.end() || !text->is_string() || !Now::Voice::line_ok(text->get<std::string>())) {
        return std::string("text is one line of 1 to 200 characters");
    }
    in.text = text->get<std::string>();
    static const std::regex hhmm("^([01][0-9]|2[0-3]):[0-5][0-9]$");
    const auto time = body.find("time");
    if (time == body.end() || !time->is_string() || !std::regex_match(time->get<std::string>(), hhmm)) {
        return std::string("time is HH:MM, like 09:00");
    }
    in.at = time->get<std::string>();
    if (const auto days = body.find("days"); days != body.end()) {
        if (!days->is_array() || days->empty() || days->size() > 7) {
            return std::string("days are 1 to 7 weekdays, Monday = 1");
        }
        for (const auto& d : *days) {
            if (!d.is_number_integer() || d.get<int>() < 1 || d.get<int>() > 7) {
                return std::string("days are 1 to 7 weekdays, Monday = 1");
            }
            in.days |= 1 << (d.get<int>() - 1);
        }
    } else {
        in.days = 127;
    }
    const auto tz = body.find("tz");
    in.tz = tz != body.end() && tz->is_string() ? tz->get<std::string>() : fallback_tz;
    if (!Repositories::Zones::is_zone(in.tz)) {
        return std::string("tz is an IANA zone like Asia/Almaty");
    }
    return in;
}

nlohmann::json process_due(const nlohmann::json& /*payload*/) {
    int said = 0;
    Repositories::Agent::InboxRepository inbox;
    for (const auto& r : Repositories::Agent::ReminderRepository().take_due()) {
        inbox.add(r.owner,
                  "remind",
                  nlohmann::json{{"text", r.text}, {"via", "schedule"}, {"reminder", r.id}},
                  std::nullopt,
                  kLineTtl,
                  "ready");
        Security::Audit::record(r.owner, "ani.reminder.said", "ani_reminder", r.id);
        ++said;
    }
    return nlohmann::json{{"status", "done"}, {"said", said}};
}

}  // namespace Agent::Reminders
