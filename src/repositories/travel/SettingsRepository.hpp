/**
 * @file SettingsRepository.hpp
 * @brief travel_settings: the owner's home country, where the days before the
 *        first crossing are spent.
 */

#pragma once

#include <optional>
#include <string>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"

namespace Repositories::Travel {

class SettingsRepository {
public:
    std::optional<std::string> home(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params("SELECT home_country FROM travel_settings WHERE owner_id = $1::uuid", owner);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    void put_home(const std::string& owner, const std::string& country) {
        detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    txn.exec_params(
                        "INSERT INTO travel_settings (owner_id, home_country) VALUES ($1::uuid, $2) "
                        "ON CONFLICT (owner_id) DO UPDATE SET home_country = EXCLUDED.home_country",
                        owner,
                        country);
                    return true;
                });
            },
            &detail::translate);
    }
};

}  // namespace Repositories::Travel
