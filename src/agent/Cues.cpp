/**
 * @file Cues.cpp
 * @brief Body for src/agent/Cues.hpp.
 */

#include "agent/Cues.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include "utils/Date.hpp"
#include "utils/Utf8.hpp"

namespace Agent::Cues {

namespace {

using json = nlohmann::json;

constexpr std::string_view kAgentPrefix = "agent-";
/// The longest label of a button and the longest payload, as JSON.
constexpr std::size_t kLabelMax = 30;
constexpr std::size_t kPayloadMax = 1000;

/// A meal's title as the Food screen names it.
std::string meal_title(const std::string& key) {
    if (key == "breakfast") {
        return "Завтрак";
    }
    if (key == "lunch") {
        return "Обед";
    }
    if (key == "snack") {
        return "Перекус";
    }
    if (key == "dinner") {
        return "Ужин";
    }
    return key;
}

bool logged(const std::set<std::string>& meals, const std::string& key) {
    return meals.count(key) > 0;
}

/// Whole days from @p from to @p to (both YYYY-MM-DD); nullopt when one is not a day.
std::optional<int> days_between(const std::string& from, const std::string& to) {
    try {
        const auto a = std::chrono::sys_days{Utils::Date::parse_ymd(from)};
        const auto b = std::chrono::sys_days{Utils::Date::parse_ymd(to)};
        return static_cast<int>((b - a).count());
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
}

bool is_uuid(std::string_view s) {
    if (s.size() != 36) {
        return false;
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return false;
            }
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string plural(int n, const char* one, const char* few, const char* many) {
    const int tens = std::abs(n) % 100;
    const int ones = std::abs(n) % 10;
    const char* word = many;
    if (ones == 1 && tens != 11) {
        word = one;
    } else if (ones >= 2 && ones <= 4 && !(tens >= 12 && tens <= 14)) {
        word = few;
    }
    return std::to_string(n) + " " + word;
}

std::string duration(int minutes) {
    const int m = std::max(minutes, 0);
    if (m < 60) {
        return std::to_string(m) + " мин";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d ч %02d", m / 60, m % 60);
    return buf;
}

std::string thousands(long long n) {
    const bool negative = n < 0;
    std::string digits = std::to_string(negative ? -n : n);
    std::string out;
    const std::size_t lead = digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && i >= lead && (i - lead) % 3 == 0) {
            out += "\xE2\x80\xAF";  // U+202F, a narrow no-break space
        }
        out += digits[i];
    }
    return negative ? "-" + out : out;
}

std::string currency_sign(const std::string& code) {
    static const std::pair<const char*, const char*> kSigns[] = {
        {"KZT", "₸"}, {"USD", "$"}, {"EUR", "€"}, {"RUB", "₽"}, {"GBP", "£"}, {"GEL", "₾"}, {"TRY", "₺"}, {"THB", "฿"}};
    for (const auto& [c, sign] : kSigns) {
        if (code == c) {
            return sign;
        }
    }
    return code;
}

std::vector<Cue> rules(const Input& in) {
    std::vector<Cue> out;
    const int minute = in.minute;

    // 1. A meeting in 15 to 60 minutes, lunch time, lunch not written.
    if (in.meals && !logged(*in.meals, "lunch") && minute >= 11 * 60 && minute < 16 * 60) {
        const auto soon =
            std::find_if(in.meetings.begin(), in.meetings.end(), [](int m) { return m >= 15 && m <= 60; });
        if (soon != in.meetings.end()) {
            out.push_back(Cue{"meeting-soon",
                              "today",
                              "Через " + std::to_string(*soon) + " минут созвон. Пообедаем до него?",
                              Action{"Обед", "food", json{{"meal", "lunch"}}}});
        }
    }

    // 2. The latest meal whose time has passed, when nothing is written for
    // it; an earlier one skipped is not brought up once a later is in.
    if (in.meals) {
        static const std::pair<const char*, int> kDue[] = {
            {"dinner", 20 * 60}, {"lunch", 13 * 60 + 30}, {"breakfast", 10 * 60 + 30}};
        for (const auto& [key, after] : kDue) {
            if (minute < after) {
                continue;
            }
            if (!logged(*in.meals, key)) {
                const std::string title = meal_title(key);
                out.push_back(Cue{std::string("meal-missing-") + key,
                                  "food",
                                  title + " еще не записан. Запишем?",
                                  Action{title, "food", json{{"meal", key}}}});
            }
            break;
        }
    }

    // 3. A night under six hours.
    if (in.sleep_minutes && *in.sleep_minutes < 360) {
        out.push_back(Cue{"sleep-short",
                          "body",
                          "Сон " + duration(*in.sleep_minutes) + ". Сегодня полегче: без тяжелых подходов?",
                          std::nullopt});
    }

    // 4. The tape: over 10 days since the last measurement, or none yet.
    if (in.body) {
        if (in.last_measure) {
            const auto days = days_between(*in.last_measure, in.date);
            if (days && *days > 10) {
                out.push_back(Cue{"measure-stale",
                                  "body",
                                  "Талию не мерили " + plural(*days, "день", "дня", "дней") + ". Сделаем сегодня?",
                                  Action{"Замер", "measure", json()}});
            }
        } else {
            out.push_back(
                Cue{"measure-stale", "body", "Замеров еще нет. Сделаем первый?", Action{"Замер", "measure", json()}});
        }
    }

    // 5. The first category over its budget, in its own currency.
    if (in.money) {
        const auto over = std::find_if(
            in.money->begin(), in.money->end(), [](const Category& c) { return c.budget && c.spent > *c.budget; });
        if (over != in.money->end()) {
            const long long by = std::llround(over->spent - *over->budget);
            out.push_back(Cue{"budget-over",
                              "money",
                              "«" + over->title + "» уже на " + thousands(by) + " " + currency_sign(over->currency) +
                                  " выше бюджета.",
                              std::nullopt});
        }
    }
    return out;
}

json to_json(const Cue& c, const std::string& expires_at) {
    json action;
    if (c.action) {
        action = json{{"label", c.action->label}, {"kind", c.action->kind}};
        if (!c.action->payload.is_null()) {
            action["payload"] = c.action->payload;
        }
    }
    return json{{"id", c.id}, {"screen", c.screen}, {"text", c.text}, {"action", action}, {"expires_at", expires_at}};
}

Cue from_row(const json& row) {
    Cue c;
    c.id = std::string(kAgentPrefix) + row.value("id", std::string());
    c.screen = row.value("screen", std::string("today"));
    c.text = row.value("text", std::string());
    const json& a = row.contains("action") ? row["action"] : json();
    if (a.is_object()) {
        c.action = Action{a.value("label", std::string()),
                          a.value("kind", std::string()),
                          a.contains("payload") ? a["payload"] : json()};
    }
    return c;
}

bool is_rule_id(std::string_view id) {
    return std::find(kRuleIds.begin(), kRuleIds.end(), id) != kRuleIds.end();
}

std::optional<std::string> agent_uuid(std::string_view id) {
    if (id.substr(0, kAgentPrefix.size()) != kAgentPrefix) {
        return std::nullopt;
    }
    const std::string_view rest = id.substr(kAgentPrefix.size());
    if (!is_uuid(rest)) {
        return std::nullopt;
    }
    return std::string(rest);
}

std::optional<std::string> action_problem(const json& action) {
    if (action.is_null()) {
        return std::nullopt;
    }
    if (!action.is_object()) {
        return "action is an object {label, kind, payload?} or null";
    }
    for (auto it = action.begin(); it != action.end(); ++it) {
        if (it.key() != "label" && it.key() != "kind" && it.key() != "payload") {
            return "action has only label, kind and payload";
        }
    }
    const std::string label =
        action.contains("label") && action["label"].is_string() ? action["label"].get<std::string>() : "";
    const std::size_t n = Utils::Utf8::length(label);
    if (n < 1 || n > kLabelMax) {
        return "action.label is 1 to 30 characters";
    }
    const std::string kind =
        action.contains("kind") && action["kind"].is_string() ? action["kind"].get<std::string>() : "";
    if (std::find(kKinds.begin(), kKinds.end(), kind) == kKinds.end()) {
        return "action.kind is food, spend, task, water, measure or set";
    }
    if (action.contains("payload") && !action["payload"].is_null()) {
        if (!action["payload"].is_object()) {
            return "action.payload is an object";
        }
        if (action["payload"].dump().size() > kPayloadMax) {
            return "action.payload is at most 1000 characters as JSON";
        }
    }
    return std::nullopt;
}

}  // namespace Agent::Cues
