/**
 * @file Digest.cpp
 * @brief Body for src/travel/now/Digest.hpp — compiled once into app_core.
 */

#include "travel/now/Digest.hpp"

#include <nlohmann/json.hpp>

#include "now/Today.hpp"
#include "repositories/travel/Counters.hpp"
#include "repositories/travel/DocumentRepository.hpp"
#include "repositories/travel/TripRepository.hpp"
#include "travel/Documents.hpp"
#include "utils/Config.hpp"

namespace Travel {

nlohmann::json digest(const std::string& owner, const std::string& day) {
    return ::Now::Today::travel_block(
        Repositories::Travel::TripRepository().next(owner, day).value_or(nlohmann::json()),
        Repositories::Travel::counters_json(owner, day),
        day);
}

nlohmann::json documents_digest(const std::string& owner, const std::string& day) {
    const int warn_days =
        Config::is_initialized()
            ? Config::get().get<int>("travel.documents_warn_days", "TRAVEL_DOCUMENTS_WARN_DAYS", kDocumentsWarnDays)
            : kDocumentsWarnDays;
    nlohmann::json out = Repositories::Travel::DocumentRepository().expiring(owner, day, warn_days);
    return out.empty() ? nlohmann::json() : out;
}

}  // namespace Travel
