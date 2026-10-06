/**
 * @file ParseAnswer.hpp
 * @brief The answer schema of the food parse: what the model must return,
 *        checked before anything is stored.
 *
 * Pure: no database, no network. The item ids the model may reference are
 * handed in by the caller (the user's own items).
 */

#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Food::Parse {

struct Line {
    std::string name;
    double grams = 0;
    double kcal = 0;
    double protein_g = 0;
    double fat_g = 0;
    double carbs_g = 0;
    std::optional<std::string> item_id;
    bool estimated = true;
    std::string note;
};

/// The answer does not fit the schema; what() names the first problem.
struct Invalid : std::runtime_error {
    explicit Invalid(const std::string& what) : std::runtime_error(what) {}
};

inline constexpr int kMaxLines = 50;
inline constexpr std::size_t kNameMax = 120;
inline constexpr std::size_t kNoteMax = 500;
inline constexpr double kGramsMax = 10000;
inline constexpr double kKcalMax = 10000;
inline constexpr double kMacroMax = 1000;

namespace detail {

/// The JSON text with a ```json fence removed, when the model added one.
inline std::string unfenced(std::string_view content) {
    const auto first = content.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = content.find_last_not_of(" \t\r\n");
    std::string_view body = content.substr(first, last - first + 1);
    if (body.rfind("```", 0) == 0) {
        const auto line_end = body.find('\n');
        const auto close = body.rfind("```");
        if (line_end != std::string_view::npos && close != std::string_view::npos && close > line_end) {
            body = body.substr(line_end + 1, close - line_end - 1);
        }
    }
    return std::string(body);
}

inline double number(
    const nlohmann::json& line, const char* key, double lo, double hi, bool required, double fallback) {
    if (!line.contains(key) || line[key].is_null()) {
        if (required) {
            throw Invalid(std::string("missing ") + key);
        }
        return fallback;
    }
    if (!line[key].is_number()) {
        throw Invalid(std::string(key) + " is not a number");
    }
    const double v = line[key].get<double>();
    if (!(v >= lo && v <= hi)) {
        throw Invalid(std::string(key) + " is out of range");
    }
    return v;
}

inline bool is_uuid(std::string_view s) {
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

}  // namespace detail

/**
 * @brief The lines of the model's answer (the content of its message).
 * @param own_item_ids the ids a line may reference; any other id is invalid.
 * @throws Invalid
 */
inline std::vector<Line> parse_answer(std::string_view content, const std::set<std::string>& own_item_ids) {
    const nlohmann::json root = nlohmann::json::parse(detail::unfenced(content), nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        throw Invalid("answer is not a JSON object");
    }
    if (!root.contains("lines") || !root["lines"].is_array()) {
        throw Invalid("answer has no lines array");
    }
    const auto& lines = root["lines"];
    if (lines.empty()) {
        throw Invalid("answer has no lines");
    }
    if (lines.size() > static_cast<std::size_t>(kMaxLines)) {
        throw Invalid("answer has too many lines");
    }
    std::vector<Line> out;
    for (const auto& l : lines) {
        if (!l.is_object()) {
            throw Invalid("a line is not an object");
        }
        Line line;
        if (!l.contains("name") || !l["name"].is_string()) {
            throw Invalid("a line has no name");
        }
        line.name = l["name"].get<std::string>();
        if (line.name.empty() || line.name.size() > kNameMax) {
            throw Invalid("a name is empty or too long");
        }
        line.grams = detail::number(l, "grams", 0.01, kGramsMax, true, 0);
        line.kcal = detail::number(l, "kcal", 0, kKcalMax, true, 0);
        line.protein_g = detail::number(l, "protein_g", 0, kMacroMax, false, 0);
        line.fat_g = detail::number(l, "fat_g", 0, kMacroMax, false, 0);
        line.carbs_g = detail::number(l, "carbs_g", 0, kMacroMax, false, 0);
        if (l.contains("item_id") && !l["item_id"].is_null()) {
            if (!l["item_id"].is_string()) {
                throw Invalid("item_id is not a string");
            }
            const std::string id = l["item_id"].get<std::string>();
            if (!detail::is_uuid(id) || own_item_ids.count(id) == 0) {
                throw Invalid("item_id is not one of the user's items");
            }
            line.item_id = id;
        }
        line.estimated = !l.contains("estimated") || !l["estimated"].is_boolean() || l["estimated"].get<bool>();
        if (l.contains("note") && l["note"].is_string()) {
            line.note = l["note"].get<std::string>().substr(0, kNoteMax);
        }
        out.push_back(std::move(line));
    }
    return out;
}

/// The lines as the job stores them and the page reads them.
inline nlohmann::json to_json(const std::vector<Line>& lines) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& l : lines) {
        out.push_back({{"name", l.name},
                       {"grams", l.grams},
                       {"kcal", l.kcal},
                       {"protein_g", l.protein_g},
                       {"fat_g", l.fat_g},
                       {"carbs_g", l.carbs_g},
                       {"item_id", l.item_id ? nlohmann::json(*l.item_id) : nlohmann::json()},
                       {"estimated", l.estimated},
                       {"note", l.note}});
    }
    return out;
}

}  // namespace Food::Parse
