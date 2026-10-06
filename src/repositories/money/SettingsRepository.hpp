/**
 * @file SettingsRepository.hpp
 * @brief The per-user settings of the money section (money_settings): the
 *        "as if" view currency and the advisor's switches.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class SettingsRepository {
public:
    struct Input {
        std::optional<std::string> view_currency;
        bool advisor_enabled = false;
        int advisor_weekday = 1;
        std::vector<std::string> advisor_currencies;
        std::string advisor_note;
    };

    /// The row, or the defaults for a user without one.
    nlohmann::json load(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT row_to_json(t) FROM (SELECT " + kColumns + " FROM money_settings WHERE owner_id = $1::uuid) t",
                owner);
            if (r.empty()) {
                return nlohmann::json{{"view_currency", nullptr},
                                      {"advisor_enabled", false},
                                      {"advisor_weekday", 1},
                                      {"advisor_currencies", nlohmann::json::array()},
                                      {"advisor_note", ""},
                                      {"updated_at", nullptr}};
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// @throws Invariant on a bad currency code or weekday.
    nlohmann::json put(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    nlohmann::json currencies = in.advisor_currencies;
                    txn.exec_params(
                        "INSERT INTO money_settings (owner_id, view_currency, advisor_enabled, advisor_weekday, "
                        " advisor_currencies, advisor_note) "
                        "VALUES ($1::uuid, $2, $3, $4, (SELECT COALESCE(array_agg(x), '{}') FROM "
                        " json_array_elements_text($5::json) x), $6) "
                        "ON CONFLICT (owner_id) DO UPDATE SET view_currency = EXCLUDED.view_currency, "
                        " advisor_enabled = EXCLUDED.advisor_enabled, advisor_weekday = EXCLUDED.advisor_weekday, "
                        " advisor_currencies = EXCLUDED.advisor_currencies, advisor_note = EXCLUDED.advisor_note, "
                        " updated_at = now()",
                        owner,
                        in.view_currency,
                        in.advisor_enabled,
                        in.advisor_weekday,
                        currencies.dump(),
                        in.advisor_note);
                    auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + kColumns +
                                                 " FROM money_settings WHERE owner_id = $1::uuid) t",
                                             owner);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// Owners with the advisor switched on for the given weekday (1 = Monday).
    std::vector<std::string> advisor_owners(int weekday) {
        return Database::get().execute_read([&](auto& txn) {
            std::vector<std::string> out;
            auto r = txn.exec_params(
                "SELECT owner_id::text FROM money_settings WHERE advisor_enabled AND advisor_weekday = $1", weekday);
            for (const auto& row : r) {
                out.push_back(row[0].template as<std::string>());
            }
            return out;
        });
    }

private:
    inline static const std::string kColumns =
        "view_currency, advisor_enabled, advisor_weekday, to_json(advisor_currencies) AS advisor_currencies, "
        "advisor_note, to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";
};

}  // namespace Repositories::Money
