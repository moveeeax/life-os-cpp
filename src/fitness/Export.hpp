/**
 * @file Export.hpp
 * @brief Выгрузка данных здоровья: конверт schema_version 1.0 и CSV.
 * @details Не-HTTP часть бывшего DataController::exportData из mi-fitness-api:
 *          конверт совместим с python-мостом (export.py), CSV экранирует
 *          формулы по правилам _escape_csv_value. Контроллер только
 *          разбирает параметры и выбирает формат.
 */

#pragma once

#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Fitness::Export {

/// Имена выгружаемых наборов, в порядке моста.
const std::vector<std::string>& types();

/// JSON-строки в CSV. Правила моста (_escape_csv_value): lstrip перед
/// проверкой, префиксы = + - @ TAB CR получают апостроф, экранируются только
/// строковые значения; числа не трогаются.
std::string to_csv(const nlohmann::json& rows);

/// Конверт выгрузки. @p type пустой означает все наборы (dataset = null).
nlohmann::json envelope(const nlohmann::json& records,
                        const std::string& type,
                        const std::string& from,
                        const std::string& to,
                        long long now_epoch);

}  // namespace Fitness::Export
