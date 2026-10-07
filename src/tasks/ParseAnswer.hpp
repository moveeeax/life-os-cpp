/**
 * @file ParseAnswer.hpp
 * @brief The rules a tasks_parse answer must meet: `{"lines": [...]}`, 1..20
 *        lines, a title, one of the six areas, a calendar day or null. An
 *        effort outside the three is dropped rather than failing the answer.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "tasks/Fields.hpp"
#include "utils/Date.hpp"
#include "utils/Utf8.hpp"

namespace Tasks::Parse {

struct Line {
    std::string title;
    std::string area;
    std::optional<std::string> effort;
    std::optional<std::string> due;
    std::string next_step;
    double confidence = 0.5;
    bool possible_duplicate = false;
};

struct Invalid : std::runtime_error {
    explicit Invalid(const std::string& what) : std::runtime_error(what) {}
};

inline constexpr int kMaxLines = 20;

namespace detail {

/// The answer without a ```json fence around it, if the model added one.
inline std::string unfenced(std::string_view content) {
    std::string_view body = content;
    while (!body.empty() && (body.front() == ' ' || body.front() == '\n' || body.front() == '\r')) {
        body.remove_prefix(1);
    }
    if (body.rfind("```", 0) == 0) {
        const auto line_end = body.find('\n');
        const auto close = body.rfind("```");
        if (line_end != std::string_view::npos && close != std::string_view::npos && close > line_end) {
            body = body.substr(line_end + 1, close - line_end - 1);
        }
    }
    return std::string(body);
}

/// Lower case for ASCII and Russian Cyrillic (А-Я, Ё), matching what the
/// database's lower() gives the open titles; other bytes are kept.
inline std::string folded(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(std::tolower(c)));
            continue;
        }
        const auto next = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
        if (c == 0xD0 && next >= 0x90 && next <= 0x9F) {  // А..П -> а..п
            out.push_back(static_cast<char>(0xD0));
            out.push_back(static_cast<char>(next + 0x20));
            ++i;
        } else if (c == 0xD0 && next >= 0xA0 && next <= 0xAF) {  // Р..Я -> р..я
            out.push_back(static_cast<char>(0xD1));
            out.push_back(static_cast<char>(next - 0x20));
            ++i;
        } else if (c == 0xD0 && next == 0x81) {  // Ё -> ё
            out.push_back(static_cast<char>(0xD1));
            out.push_back(static_cast<char>(0x91));
            ++i;
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

inline std::string text_of(const nlohmann::json& o, const char* key) {
    return o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : std::string();
}

}  // namespace detail

inline std::vector<Line> parse_answer(std::string_view content, const std::set<std::string>& open_titles) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(detail::unfenced(content));
    } catch (const nlohmann::json::exception&) {
        throw Invalid("the answer is not JSON");
    }
    if (!doc.is_object() || !doc.contains("lines") || !doc["lines"].is_array()) {
        throw Invalid("the answer has no lines array");
    }
    const auto& raw = doc["lines"];
    if (raw.empty() || raw.size() > static_cast<std::size_t>(kMaxLines)) {
        throw Invalid("the answer must hold 1.." + std::to_string(kMaxLines) + " lines");
    }
    std::vector<Line> out;
    for (const auto& o : raw) {
        if (!o.is_object()) {
            throw Invalid("a line is not an object");
        }
        Line l;
        l.title = Utils::Utf8::cut(detail::text_of(o, "title"), Fields::kTitleMax);
        if (l.title.empty()) {
            throw Invalid("a line has no title");
        }
        l.area = detail::text_of(o, "area");
        if (!Fields::is_area(l.area)) {
            throw Invalid("a line has an area outside the six: " + l.area);
        }
        if (const auto e = detail::text_of(o, "effort"); Fields::is_effort(e)) {
            l.effort = e;
        }
        if (o.contains("due") && o["due"].is_string()) {
            const auto d = o["due"].get<std::string>();
            try {
                (void)Utils::Date::parse_ymd(d);
            } catch (const std::invalid_argument&) {
                throw Invalid("a line has a due that is not a calendar day: " + d);
            }
            l.due = d;
        }
        l.next_step = Utils::Utf8::cut(detail::text_of(o, "next_step"), Fields::kNextStepMax);
        if (o.contains("confidence") && o["confidence"].is_number()) {
            l.confidence = std::clamp(o["confidence"].get<double>(), 0.0, 1.0);
        }
        l.possible_duplicate = open_titles.count(detail::folded(l.title)) > 0;
        out.push_back(std::move(l));
    }
    return out;
}

inline nlohmann::json to_json(const std::vector<Line>& lines) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& l : lines) {
        out.push_back({{"title", l.title},
                       {"area", l.area},
                       {"effort", l.effort ? nlohmann::json(*l.effort) : nlohmann::json()},
                       {"due", l.due ? nlohmann::json(*l.due) : nlohmann::json()},
                       {"next_step", l.next_step},
                       {"confidence", l.confidence},
                       {"possible_duplicate", l.possible_duplicate}});
    }
    return out;
}

}  // namespace Tasks::Parse
