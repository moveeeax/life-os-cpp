/**
 * @file Export.hpp
 * @brief Health data export: schema_version 1.0 envelope and CSV.
 * @details The non-HTTP part of the former DataController::exportData from
 *          mi-fitness-api: the envelope is compatible with the python bridge
 *          (export.py), CSV escapes formulas per the _escape_csv_value rules.
 *          The controller only parses parameters and picks the format.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Fitness::Export {

/// Names of the exported datasets, in bridge order.
const std::vector<std::string>& types();

/// JSON rows to CSV. Bridge rules (_escape_csv_value): lstrip before the
/// check, prefixes = + - @ TAB CR get an apostrophe, only string values are
/// escaped; numbers are left alone.
std::string to_csv(const nlohmann::json& rows);

/// Export envelope. Empty @p type means all datasets (dataset = null).
nlohmann::json envelope(const nlohmann::json& records,
                        const std::string& type,
                        const std::string& from,
                        const std::string& to,
                        long long now_epoch);

}  // namespace Fitness::Export
