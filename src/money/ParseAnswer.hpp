/**
 * @file ParseAnswer.hpp
 * @brief The rules a money_parse answer must meet before its lines are shown:
 *        `{"lines": [...]}`, 1..100 lines, positive amounts, calendar dates,
 *        names cut by characters. An account or category id that is not the
 *        user's is set to null with a note instead of failing the whole
 *        answer: one unmatched card must not lose the other lines.
 */

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "utils/Date.hpp"
#include "utils/Utf8.hpp"

namespace Money::Parse {

struct Line {
    std::string type = "expense";  // income | expense
    std::string date;
    std::optional<std::string> time;
    std::optional<std::string> account_id;
    double amount = 0;
    std::string merchant;
    std::string name;
    std::optional<std::string> category_id;
    std::optional<double> receipt_amount;
    std::optional<std::string> receipt_currency;
    std::string fx_note;
    double confidence = 0.5;
    std::string note;
};

struct Invalid : std::runtime_error {
    explicit Invalid(const std::string& what) : std::runtime_error(what) {}
};

inline constexpr int kMaxLines = 100;
inline constexpr std::size_t kNameMax = 200;
inline constexpr std::size_t kNoteMax = 500;
inline constexpr double kAmountMax = 1e9;

namespace detail {

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

inline std::optional<std::string> text(const nlohmann::json& line, const char* key) {
    if (line.contains(key) && line[key].is_string() && !line[key].get<std::string>().empty()) {
        return line[key].get<std::string>();
    }
    return std::nullopt;
}

inline bool is_date(const std::string& s) {
    try {
        Utils::Date::parse_ymd(s);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

inline void append_note(std::string& note, const char* what) {
    note = note.empty() ? std::string(what) : note + "; " + what;
}

}  // namespace detail

/**
 * @param content      The model's message.
 * @param hint_date    The date a line without one takes.
 * @param own_accounts Ids of the user's accounts.
 * @param own_categories Category id -> kind ("income" | "expense") of the user's categories.
 * @throws Invalid
 */
inline std::vector<Line> parse_answer(std::string_view content,
                                      const std::string& hint_date,
                                      const std::set<std::string>& own_accounts,
                                      const std::map<std::string, std::string>& own_categories) {
    const nlohmann::json root = nlohmann::json::parse(detail::unfenced(content), nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object() || !root.contains("lines") || !root["lines"].is_array()) {
        throw Invalid("answer has no lines array");
    }
    const auto& lines = root["lines"];
    if (lines.empty() || lines.size() > static_cast<std::size_t>(kMaxLines)) {
        throw Invalid("lines must hold 1..100 entries");
    }
    static const std::regex kTime(R"(^([01]\d|2[0-3]):[0-5]\d$)");
    static const std::regex kCode(R"(^[A-Z]{3}$)");
    std::vector<Line> out;
    for (const auto& l : lines) {
        if (!l.is_object()) {
            throw Invalid("a line is not an object");
        }
        Line line;
        if (const auto t = detail::text(l, "type")) {
            if (*t != "income" && *t != "expense") {
                throw Invalid("type must be income or expense");
            }
            line.type = *t;
        }
        line.date = detail::text(l, "date").value_or(hint_date);
        if (!detail::is_date(line.date)) {
            throw Invalid("a date is not a calendar day");
        }
        if (const auto t = detail::text(l, "time"); t && std::regex_match(*t, kTime)) {
            line.time = *t;
        }
        if (!l.contains("amount") || !l["amount"].is_number()) {
            throw Invalid("a line has no amount");
        }
        line.amount = l["amount"].get<double>();
        if (!(line.amount > 0 && line.amount <= kAmountMax)) {
            throw Invalid("an amount is not above 0");
        }
        line.merchant = Utils::Utf8::cut(detail::text(l, "merchant").value_or(""), kNameMax);
        line.name = Utils::Utf8::cut(detail::text(l, "name").value_or(line.merchant), kNameMax);
        if (line.name.empty()) {
            throw Invalid("a line has no name");
        }
        line.note = Utils::Utf8::cut(detail::text(l, "note").value_or(""), kNoteMax);
        line.fx_note = Utils::Utf8::cut(detail::text(l, "fx_note").value_or(""), 200);
        if (const auto a = detail::text(l, "account_id")) {
            if (own_accounts.count(*a)) {
                line.account_id = *a;
            } else {
                detail::append_note(line.note, "account not matched, pick one");
            }
        }
        if (const auto c = detail::text(l, "category_id")) {
            const auto it = own_categories.find(*c);
            if (it != own_categories.end() && it->second == line.type) {
                line.category_id = *c;
            } else {
                detail::append_note(line.note, "category not matched, pick one");
            }
        }
        const bool has_amount =
            l.contains("receipt_amount") && l["receipt_amount"].is_number() && l["receipt_amount"].get<double>() > 0;
        const auto currency = detail::text(l, "receipt_currency");
        if (has_amount && currency && std::regex_match(*currency, kCode)) {
            line.receipt_amount = l["receipt_amount"].get<double>();
            line.receipt_currency = *currency;
        }
        if (l.contains("confidence") && l["confidence"].is_number()) {
            line.confidence = std::clamp(l["confidence"].get<double>(), 0.0, 1.0);
        }
        out.push_back(std::move(line));
    }
    return out;
}

inline nlohmann::json to_json(const std::vector<Line>& lines) {
    nlohmann::json out = nlohmann::json::array();
    const auto opt = [](const auto& v) { return v ? nlohmann::json(*v) : nlohmann::json(); };
    for (const auto& l : lines) {
        out.push_back({{"type", l.type},
                       {"date", l.date},
                       {"time", opt(l.time)},
                       {"account_id", opt(l.account_id)},
                       {"amount", l.amount},
                       {"merchant", l.merchant},
                       {"name", l.name},
                       {"category_id", opt(l.category_id)},
                       {"receipt_amount", opt(l.receipt_amount)},
                       {"receipt_currency", opt(l.receipt_currency)},
                       {"fx_note", l.fx_note},
                       {"confidence", l.confidence},
                       {"note", l.note}});
    }
    return out;
}

}  // namespace Money::Parse
