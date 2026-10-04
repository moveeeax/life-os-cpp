/**
 * @file Export.cpp
 * @brief Bodies for src/fitness/Export.hpp — compiled once into app_core.
 */

#include "fitness/Export.hpp"

#include <cstdint>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Normalize.hpp"

namespace Fitness::Export {

using json = nlohmann::json;

const std::vector<std::string>& types() {
    static const std::vector<std::string> kTypes = {"daily_activity",
                                                    "sleep",
                                                    "heart_rate",
                                                    "stress",
                                                    "spo2",
                                                    "body_measurements",
                                                    "workouts",
                                                    "abnormal_heart_beat"};
    return kTypes;
}

std::string to_csv(const json& rows) {
    if (!rows.is_array() || rows.empty()) {
        return "";
    }
    // A string value starting (after spaces) with = + - @ TAB CR gets an
    // apostrophe: an export opened in a spreadsheet must not execute
    // formulas. Bridge rules from _escape_csv_value; numbers are left alone,
    // otherwise negative values turn into text.
    const auto cell = [](const json& v) {
        std::string s;
        bool escapable = false;
        if (v.is_null()) {
            s = "";
        } else if (v.is_string()) {
            s = v.get<std::string>();
            escapable = true;
        } else {
            s = v.dump();
        }
        if (escapable) {
            const auto lead = s.find_first_not_of(' ');
            if (lead != std::string::npos) {
                const char c = s[lead];
                if (c == '=' || c == '+' || c == '-' || c == '@' || c == '\t' || c == '\r') {
                    s.insert(s.begin(), '\'');
                }
            }
        }
        if (s.find_first_of(",\"\n\r") != std::string::npos) {
            std::string quoted = "\"";
            for (const char c : s) {
                if (c == '\"') {
                    quoted += "\"\"";
                } else {
                    quoted += c;
                }
            }
            quoted += "\"";
            return quoted;
        }
        return s;
    };
    std::string out;
    bool first = true;
    for (const auto& [key, value] : rows[0].items()) {
        (void)value;
        if (!first) {
            out += ',';
        }
        out += key;
        first = false;
    }
    out += '\n';
    for (const auto& row : rows) {
        first = true;
        for (const auto& [key, value] : row.items()) {
            (void)key;
            if (!first) {
                out += ',';
            }
            out += cell(value);
            first = false;
        }
        out += '\n';
    }
    return out;
}

json envelope(
    const json& records, const std::string& type, const std::string& from, const std::string& to, long long now_epoch) {
    // Same envelope as the python bridge (export.py, schema_version 1.0):
    // export consumers need no changes.
    return json{{"schema_version", "1.0"},
                {"source", "life-os-cpp"},
                {"generated_at", Xiaomi::detail::iso_with_offset(static_cast<std::int64_t>(now_epoch), 0)},
                {"filters", {{"dataset", type.empty() ? json() : json(type)}, {"start_date", from}, {"end_date", to}}},
                {"records", records}};
}

}  // namespace Fitness::Export
