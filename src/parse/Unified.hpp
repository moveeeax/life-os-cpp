/**
 * @file Unified.hpp
 * @brief The unified parse (POST /api/v1/parse): one phrase, one model call,
 *        the rows of every kind it holds. The provider, the prompt and the
 *        answer are made of the three module parses, not written again.
 *
 * The kinds are those of the module parses that are on and set up (food,
 * money, tasks), plus water with the food module. The system prompt wraps the
 * modules' own prompts as sections, and the model answers
 * `{"food": {"lines": [...]}, "money": {...}, "tasks": {...}, "water": {"ml": n}}`
 * with null for a kind the phrase does not hold. Each part is checked by its
 * module's own rules (food/ParseAnswer.hpp, money/ParseAnswer.hpp,
 * tasks/ParseAnswer.hpp): a part that breaks them is dropped and named in
 * `errors`, the others stay.
 *
 * The provider is the money parse's when it is set up, else the tasks
 * parse's, else the food parse's: one call, so one provider. Its token budget
 * is the sum of the included kinds' budgets, its timeout the longest.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "food/Llm.hpp"
#include "food/ParseAnswer.hpp"
#include "llm/Chat.hpp"
#include "money/Llm.hpp"
#include "money/ParseAnswer.hpp"
#include "tasks/Llm.hpp"
#include "tasks/ParseAnswer.hpp"
#include "utils/Modules.hpp"

namespace UnifiedParse {

/// The queue job type (handler: parse/jobs/ParseJob.hpp).
inline constexpr const char* kJobType = "unified_parse";

/// The longest phrase: the food and tasks parses take 2000 characters.
inline constexpr std::size_t kTextMax = 2000;

/// The meal a phrase without one is filed under, by the hour on the person's clock.
inline std::string meal_at(int hour) {
    if (hour >= 4 && hour < 11) {
        return "breakfast";
    }
    if (hour >= 11 && hour < 16) {
        return "lunch";
    }
    if (hour >= 16 && hour < 22) {
        return "dinner";
    }
    return "snack";
}

/// The most water one phrase may add, in ml.
inline constexpr int kWaterMaxMl = 5000;

/// The module prompts the unified one is made of; an absent one leaves its kind out.
struct Prompts {
    std::optional<std::string> food, money, tasks;
};

/// Which kinds a parse covers and the provider it asks.
struct Setup {
    Chat::Settings settings;
    bool food = false, money = false, tasks = false;
    bool water() const { return food; }
};

/// The system prompt: the frame below, then each module's prompt as its section.
inline std::string compose_prompt(const Prompts& p) {
    std::string keys;
    const auto key = [&](bool on, const char* name) {
        if (on) {
            keys += std::string(keys.empty() ? "" : ", ") + "\"" + name + "\": null";
        }
    };
    key(p.food.has_value(), "food");
    key(p.money.has_value(), "money");
    key(p.tasks.has_value(), "tasks");
    key(p.food.has_value(), "water");
    std::string out =
        "You read one phrase a person typed into a single input and file each piece of it where it belongs. "
        "The phrase may mix food that was eaten, money that was spent or received, things to do and water that was "
        "drunk (\"кофе 1200 и творог 200 г, завтра позвонить маме\" is one spend, one food and one task). Answer with "
        "JSON only, no prose, no markdown, one object with these keys:\n\n{" +
        keys +
        "}\n\n"
        "A key stays null when the phrase holds nothing of its kind. Otherwise each of \"food\", \"money\" and "
        "\"tasks\" holds exactly the answer the section of the same name below asks for (an object with \"lines\"), "
        "made by that section's rules. The phrase is the user message's \"text\"; what a section calls its input "
        "(dates, items, accounts, categories, merchants, areas) is in the user message's part of the same name.\n";
    if (p.food) {
        out +=
            "\"water\" is {\"ml\": <whole millilitres>} for water the person drank (a glass is 250 and a bottle 500 "
            "unless the phrase says otherwise); tea, coffee and other drinks are not water.\n";
    }
    out +=
        "\nEach piece of the phrase goes under one key. A sum of money makes a piece a spend or an income; grams, a "
        "portion or the act of eating make it food; something to do later makes it a task. A purchase named with its "
        "price only (\"coffee 1200\") is money, not food.\n";
    const auto section = [&](const char* name, const std::optional<std::string>& prompt) {
        if (prompt) {
            out += std::string("\n## ") + name + "\n\n" + *prompt + "\n";
        }
    };
    section("food", p.food);
    section("money", p.money);
    section("tasks", p.tasks);
    return out;
}

/// The setup of the kinds that are on and configured; nullopt when none is.
inline std::optional<Setup> setup() {
    const auto food = Core::food_enabled() ? Food::Llm::settings() : std::nullopt;
    const auto money = Core::money_enabled() ? Money::Llm::parse_settings() : std::nullopt;
    const auto tasks = Core::tasks_enabled() ? Tasks::Llm::parse_settings() : std::nullopt;
    if (!food && !money && !tasks) {
        return std::nullopt;
    }
    Setup s;
    s.settings = money ? *money : (tasks ? *tasks : *food);
    s.food = food.has_value();
    s.money = money.has_value();
    s.tasks = tasks.has_value();
    Prompts prompts;
    int max_tokens = 0;
    long timeout = 0;
    for (const auto* part : {&food, &money, &tasks}) {
        if (*part) {
            max_tokens += (*part)->max_tokens;
            timeout = std::max(timeout, (*part)->timeout_seconds);
        }
    }
    if (food) {
        prompts.food = food->prompt;
    }
    if (money) {
        prompts.money = money->prompt;
    }
    if (tasks) {
        prompts.tasks = tasks->prompt;
    }
    s.settings.prompt = compose_prompt(prompts);
    s.settings.max_tokens = max_tokens;
    s.settings.timeout_seconds = timeout;
    return s;
}

/// The answer does not fit: no part was usable, what() names the first problem.
struct Invalid : std::runtime_error {
    explicit Invalid(const std::string& what) : std::runtime_error(what) {}
};

/// The user's ids and titles an answer is checked against.
struct Known {
    std::set<std::string> food_items;
    std::set<std::string> accounts;
    std::map<std::string, std::string> categories;  // id -> kind
    std::set<std::string> open_titles;
    std::string date;  // the day a money line without one takes
};

struct Parts {
    std::vector<Food::Parse::Line> food;
    std::vector<Money::Parse::Line> money;
    std::vector<Tasks::Parse::Line> tasks;
    std::optional<int> water_ml;
    nlohmann::json errors = nlohmann::json::object();  // kind -> the problem of a dropped part

    bool empty() const { return food.empty() && money.empty() && tasks.empty() && !water_ml; }
};

namespace detail {

inline std::string unfenced(std::string_view content) {
    const auto first = content.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    std::string_view body = content.substr(first);
    if (body.rfind("```", 0) == 0) {
        const auto line_end = body.find('\n');
        const auto close = body.rfind("```");
        if (line_end != std::string_view::npos && close != std::string_view::npos && close > line_end) {
            body = body.substr(line_end + 1, close - line_end - 1);
        }
    }
    return std::string(body);
}

/// A part that holds nothing: absent, null, or an object with an empty lines array.
inline bool holds_nothing(const nlohmann::json& part) {
    return part.is_null() ||
           (part.is_object() && part.contains("lines") && part["lines"].is_array() && part["lines"].empty());
}

}  // namespace detail

/**
 * The parts of the model's answer for the kinds of @p s. A part that breaks
 * its module's rules is dropped into `errors`.
 * @throws Invalid when the answer is not a JSON object, or no part is usable
 *         and one was dropped (a phrase that holds nothing gives empty parts).
 */
inline Parts parse_answer(std::string_view content, const Setup& s, const Known& known) {
    const nlohmann::json root = nlohmann::json::parse(detail::unfenced(content), nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        throw Invalid("answer is not a JSON object");
    }
    Parts out;
    const auto part = [&](const char* kind) -> const nlohmann::json* {
        if (!root.contains(kind) || detail::holds_nothing(root[kind])) {
            return nullptr;
        }
        return &root[kind];
    };
    if (s.food) {
        if (const auto* p = part("food")) {
            try {
                out.food = Food::Parse::parse_answer(p->dump(), known.food_items);
            } catch (const Food::Parse::Invalid& e) {
                out.errors["food"] = e.what();
            }
        }
    }
    if (s.money) {
        if (const auto* p = part("money")) {
            try {
                out.money = Money::Parse::parse_answer(p->dump(), known.date, known.accounts, known.categories);
            } catch (const Money::Parse::Invalid& e) {
                out.errors["money"] = e.what();
            }
        }
    }
    if (s.tasks) {
        if (const auto* p = part("tasks")) {
            try {
                out.tasks = Tasks::Parse::parse_answer(p->dump(), known.open_titles);
            } catch (const Tasks::Parse::Invalid& e) {
                out.errors["tasks"] = e.what();
            }
        }
    }
    if (s.water() && root.contains("water") && !root["water"].is_null()) {
        const auto& w = root["water"];
        if (w.is_object() && w.contains("ml") && w["ml"].is_number() && w["ml"].get<double>() >= 1 &&
            w["ml"].get<double>() <= kWaterMaxMl) {
            out.water_ml = static_cast<int>(w["ml"].get<double>() + 0.5);
        } else {
            out.errors["water"] = "water must be {\"ml\": 1.." + std::to_string(kWaterMaxMl) + "}";
        }
    }
    if (out.empty() && !out.errors.empty()) {
        throw Invalid(out.errors.begin().key() + ": " + out.errors.begin()->get<std::string>());
    }
    return out;
}

}  // namespace UnifiedParse
