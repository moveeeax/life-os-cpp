/**
 * @file CategoryRepository.hpp
 * @brief Categories with their budgets (money_categories). A budget is one
 *        amount in one currency (invariant 7); the comparison with spend
 *        happens in the reports, only in that currency.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class CategoryRepository {
public:
    struct Input {
        std::string name;
        std::string kind = "expense";
        std::string flexibility = "variable";
        std::optional<double> budget_max;
        std::optional<std::string> budget_currency;
        int position = 0;
    };

    struct Patch {
        std::optional<std::string> name, kind, flexibility;
        /// Both or neither: the pair is written together (outer: given).
        std::optional<std::optional<double>> budget_max;
        std::optional<std::optional<std::string>> budget_currency;
        std::optional<bool> archived;
        std::optional<int> position;
    };

    nlohmann::json list(const std::string& owner, bool include_archived) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + kColumns +
                    " FROM money_categories WHERE owner_id = $1::uuid AND ($2::boolean OR NOT archived) "
                    " ORDER BY kind, position, name) t",
                owner,
                include_archived);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    /// @throws Invariant (a budget without its currency, a currency not the owner's).
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO money_categories (owner_id, name, kind, flexibility, budget_max, budget_currency, "
                        " position) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7) RETURNING id::text",
                        owner,
                        in.name,
                        in.kind,
                        in.flexibility,
                        in.budget_max,
                        in.budget_currency,
                        in.position);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @throws NotFound, Invariant.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    if (p.kind.has_value()) {
                        // An expense category with expense rows cannot become an income one:
                        // the rows would stand in a category of the other kind.
                        auto used = txn.exec_params(
                            "SELECT 1 FROM money_transactions WHERE owner_id = $1::uuid AND category_id = $2::uuid "
                            " AND type <> 'fx_adjustment' AND type <> $3 LIMIT 1",
                            owner,
                            id,
                            *p.kind);
                        if (!used.empty()) {
                            throw Invariant("the category has rows of its current kind; its kind stays");
                        }
                    }
                    const bool budget_given = p.budget_max.has_value() || p.budget_currency.has_value();
                    auto r = txn.exec_params(
                        "UPDATE money_categories SET name = COALESCE($3, name), kind = COALESCE($4, kind), "
                        " flexibility = COALESCE($5, flexibility), "
                        " budget_max = CASE WHEN $6::boolean THEN $7 ELSE budget_max END, "
                        " budget_currency = CASE WHEN $6::boolean THEN $8 ELSE budget_currency END, "
                        " archived = COALESCE($9, archived), position = COALESCE($10, position), updated_at = now() "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.name,
                        p.kind,
                        p.flexibility,
                        budget_given,
                        p.budget_max.has_value() ? *p.budget_max : std::optional<double>(),
                        p.budget_currency.has_value() ? *p.budget_currency : std::optional<std::string>(),
                        p.archived,
                        p.position);
                    if (r.empty()) {
                        throw NotFound("money_category");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Deletes a category without rows; archives one that has any. @throws NotFound.
    std::string remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) -> std::string {
            auto own = txn.exec_params(
                "SELECT 1 FROM money_categories WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id);
            if (own.empty()) {
                throw NotFound("money_category");
            }
            auto used = txn.exec_params("SELECT 1 FROM money_transactions WHERE category_id = $1::uuid LIMIT 1", id);
            if (!used.empty()) {
                txn.exec_params("UPDATE money_categories SET archived = true, updated_at = now() WHERE id = $1::uuid",
                                id);
                return "archived";
            }
            txn.exec_params("UPDATE money_merchants SET category_id = NULL WHERE category_id = $1::uuid", id);
            txn.exec_params("DELETE FROM money_categories WHERE id = $1::uuid", id);
            return "deleted";
        });
    }

    /// The kind of an owner's category, or nullopt when it is not theirs.
    template <typename Txn>
    static std::optional<std::string> kind_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r =
            txn.exec_params("SELECT kind FROM money_categories WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id);
        if (r.empty()) {
            return std::nullopt;
        }
        return r[0][0].template as<std::string>();
    }

    static constexpr const char* kColumnsRaw =
        "id, name, kind, flexibility, budget_max, budget_currency, archived, position, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";

private:
    inline static const std::string kColumns = kColumnsRaw;

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + kColumns +
                                     " FROM money_categories WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Money
