/**
 * @file SettingsRepository.hpp
 * @brief The owner's Now settings: the weather city (as the geocoder found it)
 *        and the corner layout. No row means no city and the default layout.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "now/Settings.hpp"
#include "now/Weather.hpp"

namespace Repositories::Now {

class SettingsRepository {
public:
    /// {city, country_code, latitude, longitude, timezone, corners}.
    nlohmann::json get(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT row_to_json(t) FROM (SELECT city, country_code, latitude, longitude, timezone, corners "
                "FROM now_settings WHERE owner_id = $1::uuid) t",
                owner);
            nlohmann::json out = {{"city", nullptr},
                                  {"country_code", nullptr},
                                  {"latitude", nullptr},
                                  {"longitude", nullptr},
                                  {"timezone", nullptr},
                                  {"corners", nullptr}};
            if (!r.empty()) {
                out = nlohmann::json::parse(r[0][0].template as<std::string>());
            }
            if (out["corners"].is_null()) {
                out["corners"] = ::Now::Settings::default_corners();
            }
            return out;
        });
    }

    /// Sets the city, or clears it with nullopt.
    void put_place(const std::string& owner, const std::optional<::Now::Weather::Place>& p) {
        Database::get().execute_write([&](auto& txn) {
            const auto opt = [&](const std::string& s) { return p && !s.empty() ? std::optional(s) : std::nullopt; };
            txn.exec_params(
                "INSERT INTO now_settings (owner_id, city, country_code, latitude, longitude, timezone) "
                "VALUES ($1::uuid, $2, $3, $4, $5, $6) ON CONFLICT (owner_id) DO UPDATE SET city = EXCLUDED.city, "
                " country_code = EXCLUDED.country_code, latitude = EXCLUDED.latitude, "
                " longitude = EXCLUDED.longitude, timezone = EXCLUDED.timezone",
                owner,
                p ? std::optional(p->city) : std::nullopt,
                p ? opt(p->country_code) : std::nullopt,
                p ? std::optional(p->latitude) : std::nullopt,
                p ? std::optional(p->longitude) : std::nullopt,
                p ? opt(p->timezone) : std::nullopt);
            return true;
        });
    }

    /// Stores a layout already checked with Now::Settings::corners_problem.
    void put_corners(const std::string& owner, const nlohmann::json& corners) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO now_settings (owner_id, corners) VALUES ($1::uuid, $2::jsonb) "
                "ON CONFLICT (owner_id) DO UPDATE SET corners = EXCLUDED.corners",
                owner,
                corners.dump());
            return true;
        });
    }
};

}  // namespace Repositories::Now
