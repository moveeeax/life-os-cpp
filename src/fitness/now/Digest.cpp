/**
 * @file Digest.cpp
 * @brief Body for src/fitness/now/Digest.hpp — compiled once into app_core.
 */

#include "fitness/now/Digest.hpp"

#include <chrono>

#include <nlohmann/json.hpp>

#include "money/Period.hpp"
#include "now/Today.hpp"
#include "repositories/fitness/HealthReadRepository.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"
#include "repositories/food/GoalsRepository.hpp"
#include "utils/Date.hpp"

namespace Fitness {

nlohmann::json digest(const std::string& owner, const std::string& day) {
    using json = nlohmann::json;
    const std::string xiaomi = Repositories::MiAccountRepository("").xiaomi_id_of(owner).value_or("");
    json summary_row;
    json sleep_rows = json::array();
    json body_rows = json::array();
    if (!xiaomi.empty()) {
        Repositories::HealthReadRepository health(xiaomi);
        const long long offset = Repositories::region_offset_seconds(owner);
        const auto summary = health.summary(day, day, 1, 0, offset).rows;
        if (!summary.empty()) {
            summary_row = summary[0];
        }
        const auto date = std::chrono::sys_days{Utils::Date::parse_ymd(day)};
        // Sleep rows are filed by the UTC day of their end; the account's day may start hours earlier.
        sleep_rows = ::Now::Today::sleep_on_local_day(
            health.sleep(::Money::Period::detail::text_of(date - std::chrono::days{1}), day, 20, 0).rows, day, offset);
        body_rows = health.body(::Money::Period::detail::text_of(date - std::chrono::days{8}), day, 50, 0).rows;
    }
    if (body_rows.empty()) {
        // No weighing synced from the scale: the latest weight the food profile knows, if any.
        if (const auto weight = Repositories::GoalsRepository().scale_weight(owner)) {
            body_rows.push_back(json{{"timestamp", day + "T00:00:00Z"}, {"weight_kg", *weight}});
        }
    }
    return ::Now::Today::health_block(summary_row, sleep_rows, body_rows);
}

}  // namespace Fitness
