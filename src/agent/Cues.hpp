/**
 * @file Cues.hpp
 * @brief Ani's cues on the screens of the app (GET /api/v1/ani/cues): a line
 *        of hers with an optional button, by rules over the person's day. The
 *        rules are the ones the iOS app ran itself (App/Data/AniCues.swift),
 *        in the same order and with the same Russian words: a meeting soon
 *        without lunch, a meal not written by its time, a short night, the
 *        tape not used for over 10 days, a category over its budget.
 *
 *        Pure: the caller reads the data and the clock (api/AniCuesController.cpp);
 *        a source that is off or not read is nullopt and its rules stay quiet.
 *        Each rule's id is stable for the day, so a dismissal (kept per day)
 *        holds until the next local day.
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Agent::Cues {

/// The screens a cue shows on and the kinds of what its button opens.
inline const std::vector<std::string> kScreens = {"today", "food", "body", "money"};
inline const std::vector<std::string> kKinds = {"food", "spend", "task", "water", "measure", "set"};

/// The ids the rules give (the meal ones carry their meal).
inline const std::vector<std::string> kRuleIds = {"meeting-soon",
                                                  "meal-missing-breakfast",
                                                  "meal-missing-lunch",
                                                  "meal-missing-dinner",
                                                  "sleep-short",
                                                  "measure-stale",
                                                  "budget-over"};

/// A minute of the clock for a day already over, and for one not begun.
inline constexpr int kDayOver = 24 * 60;
inline constexpr int kDayAhead = -1;

/// One category of the month in its currency: the order of the list is the
/// client's (currencies by their spending, categories by theirs).
struct Category {
    std::string title;
    std::string currency;
    double spent = 0;
    std::optional<double> budget;
};

struct Input {
    /// The local day (YYYY-MM-DD) the cues are for.
    std::string date;
    /// Minutes since local midnight now on that day; kDayOver for a past day, kDayAhead for a coming one.
    int minute = 0;
    /// The meals (breakfast, lunch, snack, dinner) with an entry written; nullopt: food is not read.
    std::optional<std::set<std::string>> meals;
    /// The day's timed meetings: minutes from now to their start, in order of the start.
    std::vector<int> meetings;
    /// The night's sleep in minutes; nullopt: none known.
    std::optional<int> sleep_minutes;
    /// Whether the body sources are read (the tape rule needs them).
    bool body = false;
    /// The day of the latest tape measurement (YYYY-MM-DD); nullopt: none yet.
    std::optional<std::string> last_measure;
    /// The month's categories with spending or a budget; nullopt: money is not read.
    std::optional<std::vector<Category>> money;
};

struct Action {
    std::string label;
    std::string kind;
    nlohmann::json payload;  // null: none
};

struct Cue {
    std::string id;
    std::string screen;
    std::string text;
    std::optional<Action> action;
};

/// The rules' cues for @p in, in the client's order.
std::vector<Cue> rules(const Input& in);

/// The cue as the API gives it: {id, screen, text, action: {label, kind, payload?} | null, expires_at}.
nlohmann::json to_json(const Cue& c, const std::string& expires_at);

/// A cue the agent stored (an ani_cues row: {id, screen, text, action}) as a Cue, its id "agent-<uuid>".
Cue from_row(const nlohmann::json& row);

/// Whether @p id is one the rules give, or an agent cue's ("agent-" and a uuid).
bool is_rule_id(std::string_view id);
std::optional<std::string> agent_uuid(std::string_view id);

/// Why an agent's action is not one (a null action is fine); nullopt when it is.
std::optional<std::string> action_problem(const nlohmann::json& action);

// The words, as the app writes them (App/Data/Day.swift, App/Design/Tokens.swift).

/// "1 день", "3 дня", "12 дней".
std::string plural(int n, const char* one, const char* few, const char* many);
/// "7 ч 12" for 432 minutes, "7 ч 00" for 420, "45 мин" under an hour.
std::string duration(int minutes);
/// Thousands apart by a narrow no-break space: "3 800".
std::string thousands(long long n);
/// The sign of a currency the app knows, else its code.
std::string currency_sign(const std::string& code);

}  // namespace Agent::Cues
