/**
 * @file Digest.cpp
 * @brief Body for src/travel/now/Digest.hpp — compiled once into app_core.
 */

#include "travel/now/Digest.hpp"

#include <nlohmann/json.hpp>

#include "now/Today.hpp"
#include "repositories/travel/Counters.hpp"
#include "repositories/travel/TripRepository.hpp"

namespace Travel {

nlohmann::json digest(const std::string& owner, const std::string& day) {
    return ::Now::Today::travel_block(
        Repositories::Travel::TripRepository().next(owner, day).value_or(nlohmann::json()),
        Repositories::Travel::counters_json(owner, day),
        day);
}

}  // namespace Travel
