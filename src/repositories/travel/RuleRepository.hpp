/**
 * @file RuleRepository.hpp
 * @brief travel_rules: the owner's day counters (which countries, which window,
 *        which threshold) and the two ready templates with their sources.
 */

#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/travel/Errors.hpp"
#include "travel/Days.hpp"
#include "travel/Schengen.hpp"

namespace Repositories::Travel {

class RuleRepository {
public:
    struct Input {
        std::string name;
        std::set<std::string> countries;  // ISO codes or SCHENGEN
        std::string window_kind;          // rolling_days | rolling_12_months_in_year | since_last_entry
        std::optional<int> window_days;
        int threshold_days = 0;
        std::string threshold_kind;  // at_most | over
        bool count_crossing_days = true;
        std::string source;
    };

    /// A stored rule with what the day counter needs.
    struct Stored {
        nlohmann::json row;
        ::Travel::Rule rule;
    };

    /// The two templates: Schengen 90/180 and Kazakhstan tax residency.
    static std::optional<Input> template_of(const std::string& key) {
        if (key == "schengen") {
            return Input{"Schengen",
                         {"SCHENGEN"},
                         "rolling_days",
                         180,
                         90,
                         "at_most",
                         true,
                         "At most 90 days in any 180-day period; entry and exit days count. Regulation (EU) "
                         "2016/399, Article 6. https://www.visa-calculator.com/en/schengen-90-180-day-rule-faq"};
        }
        if (key == "kz_tax") {
            return Input{"Kazakhstan tax residency",
                         {"KZ"},
                         "rolling_12_months_in_year",
                         std::nullopt,
                         183,
                         "over",
                         true,
                         "Resident by days: more than 183 days in any 12 consecutive months ending in the tax year, "
                         "arrival and departure days included (Tax Code of Kazakhstan). The centre of vital interests "
                         "is a second ground the app does not count. "
                         "https://taxsummaries.pwc.com/kazakhstan/individual/residence"};
        }
        return std::nullopt;
    }

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO travel_rules (owner_id, name, countries, window_kind, window_days, "
                        "threshold_days, "
                        " threshold_kind, count_crossing_days, source, position) "
                        "VALUES ($1::uuid, $2, $3::text[], $4, $5, $6, $7, $8, $9, "
                        " (SELECT COALESCE(max(position), 0) + 1 FROM travel_rules WHERE owner_id = $1::uuid)) "
                        "RETURNING id::text",
                        owner,
                        in.name,
                        detail::pg_array(in.countries),
                        in.window_kind,
                        in.window_days,
                        in.threshold_days,
                        in.threshold_kind,
                        in.count_crossing_days,
                        in.source);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// A full replacement. @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE travel_rules SET name = $3, countries = $4::text[], window_kind = $5, "
                        " window_days = $6, threshold_days = $7, threshold_kind = $8, count_crossing_days = $9, "
                        " source = $10 WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.name,
                        detail::pg_array(in.countries),
                        in.window_kind,
                        in.window_days,
                        in.threshold_days,
                        in.threshold_kind,
                        in.count_crossing_days,
                        in.source);
                    if (r.empty()) {
                        throw NotFound("rule");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM travel_rules WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("rule");
            }
            return true;
        });
    }

    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            auto r =
                txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                    " FROM travel_rules WHERE owner_id = $1::uuid ORDER BY position, created_at) t",
                                owner);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// Every rule with its counter model (countries expanded).
    std::vector<Stored> all(const std::string& owner) {
        std::vector<Stored> out;
        for (const auto& row : list(owner)) {
            out.push_back({row, model_of(row)});
        }
        return out;
    }

    static ::Travel::Rule model_of(const nlohmann::json& row) {
        ::Travel::Rule r;
        std::set<std::string> codes;
        for (const auto& c : row.at("countries")) {
            codes.insert(c.get<std::string>());
        }
        r.countries = ::Travel::expand_countries(codes);
        const auto kind = row.at("window_kind").get<std::string>();
        r.window = kind == "rolling_days"                ? ::Travel::Window::RollingDays
                   : kind == "rolling_12_months_in_year" ? ::Travel::Window::Rolling12MonthsInYear
                                                         : ::Travel::Window::SinceLastEntry;
        r.window_days = row.at("window_days").is_null() ? 0 : row.at("window_days").get<int>();
        r.threshold_days = row.at("threshold_days").get<int>();
        r.kind = row.at("threshold_kind").get<std::string>() == "over" ? ::Travel::Threshold::Over
                                                                       : ::Travel::Threshold::AtMost;
        r.count_crossing_days = row.at("count_crossing_days").get<bool>();
        return r;
    }

private:
    static constexpr const char* kColumns =
        "id, name, countries, window_kind, window_days, threshold_days, threshold_kind, count_crossing_days, source, "
        "position";

    template <typename Txn>
    std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM travel_rules WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Travel
