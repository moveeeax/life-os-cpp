/**
 * @file FieldChecks.hpp
 * @brief Field checks the data controllers share beyond Validation.hpp: a
 *        calendar day, a number in a range, text measured in characters,
 *        today's date, and the "null means unset" readers.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "api/Validation.hpp"
#include "utils/Date.hpp"
#include "utils/Utf8.hpp"

namespace Api::Fields {

inline const std::regex kDateRe(R"(^\d{4}-\d{2}-\d{2}$)");

/// A real calendar day between 1900 and 2100: the regex alone lets "2026-02-30" through.
inline bool is_calendar_date(const std::string& text) {
    if (!std::regex_match(text, kDateRe)) {
        return false;
    }
    try {
        const auto ymd = Utils::Date::parse_ymd(text);
        const int year = static_cast<int>(ymd.year());
        return year >= 1900 && year <= 2100;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

/// Optional date field; null is "unset".
inline void date_field(Validation::Errors& errs, const nlohmann::json& body, const std::string& field) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string() || !is_calendar_date(body[field].get<std::string>())) {
        errs.add(field, "bad_format", "expected a calendar day as YYYY-MM-DD");
    }
}

/// Optional number within [lo, hi]; null is "unset".
inline void number_range(
    Validation::Errors& errs, const nlohmann::json& body, const std::string& field, double lo, double hi) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_number()) {
        errs.add(field, "not_number", "must be a number");
        return;
    }
    const double v = body[field].get<double>();
    if (!(v >= lo && v <= hi)) {
        errs.add(field, "out_of_range", "must be in " + nlohmann::json(lo).dump() + ".." + nlohmann::json(hi).dump());
    }
}

/// Like Validation::string_length, but in characters: the table CHECKs count
/// characters too, and a Cyrillic name must not get half the room.
inline void text_length(
    Validation::Errors& errs, const nlohmann::json& body, const std::string& field, std::size_t lo, std::size_t hi) {
    if (!body.contains(field) || body[field].is_null()) {
        return;
    }
    if (!body[field].is_string()) {
        errs.add(field, "invalid", "must be a string");
        return;
    }
    const std::size_t n = Utils::Utf8::length(body[field].get<std::string>());
    if (n < lo) {
        errs.add(field, "too_short", "min length " + std::to_string(lo));
    } else if (n > hi) {
        errs.add(field, "too_long", "max length " + std::to_string(hi));
    }
}

inline std::optional<double> opt_number(const nlohmann::json& body, const std::string& field) {
    if (body.contains(field) && body[field].is_number()) {
        return body[field].get<double>();
    }
    return std::nullopt;
}

inline std::optional<int> opt_int(const nlohmann::json& body, const std::string& field) {
    if (body.contains(field) && body[field].is_number_integer()) {
        return body[field].get<int>();
    }
    return std::nullopt;
}

inline std::string today_utc() {
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    char out[16];
    std::snprintf(out,
                  sizeof(out),
                  "%04d-%02u-%02u",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return out;
}

}  // namespace Api::Fields
