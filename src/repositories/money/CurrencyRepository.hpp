/**
 * @file CurrencyRepository.hpp
 * @brief The currencies a user keeps money in (money_currencies). Every user
 *        has their own list; a new one is seeded with the owner's ten.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class CurrencyRepository {
public:
    struct Input {
        std::string name;
        std::optional<std::string> role;  // "primary" | "local" | nullopt
        int decimals = 2;
        bool archived = false;
    };

    struct Patch {
        std::optional<std::string> name;
        std::optional<std::optional<std::string>> role;  // outer: given; inner: null clears
        std::optional<int> decimals;
        std::optional<bool> archived;
    };

    /// The owner's ten, as in the Notion base; seeded once per user.
    struct Seed {
        const char* code;
        const char* name;
        const char* role;  // nullptr for none
        int decimals;
    };
    static constexpr Seed kDefaults[] = {
        {"KZT", "Kazakhstani tenge", "primary", 2},
        {"RUB", "Russian ruble", "primary", 2},
        {"USD", "US dollar", "primary", 2},
        {"EUR", "Euro", "primary", 2},
        {"GBP", "Pound sterling", "primary", 2},
        {"TRY", "Turkish lira", "local", 2},
        {"VND", "Vietnamese dong", "local", 0},
        {"CNY", "Chinese yuan", "local", 2},
        {"THB", "Thai baht", "local", 2},
        {"SGD", "Singapore dollar", nullptr, 2},
    };

    nlohmann::json list(const std::string& owner, bool include_archived) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + kColumns +
                    " FROM money_currencies WHERE owner_id = $1::uuid AND ($2::boolean OR NOT archived) "
                    " ORDER BY CASE role WHEN 'primary' THEN 0 WHEN 'local' THEN 1 ELSE 2 END, code) t",
                owner,
                include_archived);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    std::optional<nlohmann::json> find(const std::string& owner, const std::string& code) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, code); });
    }

    /// Insert or update by code. @throws Invariant on a bad code or decimals.
    nlohmann::json upsert(const std::string& owner, const std::string& code, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    txn.exec_params(
                        "INSERT INTO money_currencies (owner_id, code, name, role, decimals, archived) "
                        "VALUES ($1::uuid, $2, $3, $4, $5, $6) "
                        "ON CONFLICT (owner_id, code) DO UPDATE SET name = EXCLUDED.name, role = EXCLUDED.role, "
                        " decimals = EXCLUDED.decimals, archived = EXCLUDED.archived",
                        owner,
                        code,
                        in.name,
                        in.role,
                        in.decimals,
                        in.archived);
                    return *find_in(txn, owner, code);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound, Invariant.
    nlohmann::json patch(const std::string& owner, const std::string& code, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE money_currencies SET name = COALESCE($3, name), "
                        " role = CASE WHEN $4::boolean THEN $5 ELSE role END, "
                        " decimals = COALESCE($6, decimals), archived = COALESCE($7, archived) "
                        "WHERE owner_id = $1::uuid AND code = $2 RETURNING code",
                        owner,
                        code,
                        p.name,
                        p.role.has_value(),
                        p.role.has_value() ? *p.role : std::optional<std::string>(),
                        p.decimals,
                        p.archived);
                    if (r.empty()) {
                        throw NotFound("money_currency");
                    }
                    return *find_in(txn, owner, code);
                });
            },
            &detail::translate);
    }

    /// The owner's ten for a user who has none yet; a user with any row keeps theirs.
    void seed_defaults(const std::string& owner) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("SELECT 1 FROM money_currencies WHERE owner_id = $1::uuid LIMIT 1", owner);
            if (!r.empty()) {
                return false;
            }
            for (const auto& d : kDefaults) {
                txn.exec_params(
                    "INSERT INTO money_currencies (owner_id, code, name, role, decimals) VALUES ($1::uuid, $2, $3, $4, "
                    "$5) ON CONFLICT DO NOTHING",
                    owner,
                    std::string(d.code),
                    std::string(d.name),
                    d.role ? std::optional<std::string>(d.role) : std::optional<std::string>(),
                    d.decimals);
            }
            return true;
        });
    }

    static constexpr const char* kColumnsRaw = "code, name, role, decimals, archived";

private:
    inline static const std::string kColumns = kColumnsRaw;

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& code) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + kColumns +
                                     " FROM money_currencies WHERE owner_id = $1::uuid AND code = $2) t",
                                 owner,
                                 code);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Money
