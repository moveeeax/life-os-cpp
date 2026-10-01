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
    // Строковое значение, начинающееся (после пробелов) с = + - @ TAB CR,
    // получает апостроф: открытая в таблице выгрузка не должна исполнять
    // формулы. Правила _escape_csv_value моста; числа не трогаются, иначе
    // отрицательные величины превращаются в текст.
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
    // Конверт как у python-моста (export.py, schema_version 1.0): потребители
    // выгрузки не переучиваются.
    return json{{"schema_version", "1.0"},
                {"source", "life-os-cpp"},
                {"generated_at", Xiaomi::detail::iso_with_offset(static_cast<std::int64_t>(now_epoch), 0)},
                {"filters", {{"dataset", type.empty() ? json() : json(type)}, {"start_date", from}, {"end_date", to}}},
                {"records", records}};
}

}  // namespace Fitness::Export
