/**
 * @file ItemRepository.hpp
 * @brief Products and dishes of the food module (table food_items).
 *
 * Rows belong to a user. An item that entries reference is archived instead
 * of deleted: the diary keeps its copied numbers, and the item stays
 * readable for them. JSON is built in SQL, as in the workout repositories.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"

namespace Repositories {

namespace detail {
/// `%`, `_` and the escape itself quoted for an ILIKE ... ESCAPE '\\' pattern.
inline std::string escape_like(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}
}  // namespace detail

struct FoodItemNotFound : NotFoundError {
    FoodItemNotFound() : NotFoundError("food_item") {}
};

class ItemRepository {
public:
    struct Input {
        std::string source = "custom";
        std::string name;
        std::string brand;
        std::string per = "100g";
        double kcal = 0;
        double protein_g = 0;
        double fat_g = 0;
        double carbs_g = 0;
        std::optional<double> fiber_g;
        std::optional<double> sugar_g;
        std::optional<double> salt_g;
        std::string servings_json = "[]";
        std::optional<std::string> off_code;
    };

    /// nullopt leaves the column as it is.
    struct Patch {
        std::optional<std::string> name;
        std::optional<std::string> brand;
        std::optional<std::string> per;
        std::optional<double> kcal;
        std::optional<double> protein_g;
        std::optional<double> fat_g;
        std::optional<double> carbs_g;
        std::optional<std::optional<double>> fiber_g;
        std::optional<std::optional<double>> sugar_g;
        std::optional<std::optional<double>> salt_g;
        std::optional<std::string> servings_json;
        std::optional<bool> archived;
    };

    struct Page {
        nlohmann::json rows = nlohmann::json::array();
        long total = 0;
    };

    /// Own items by name or brand, case-insensitive; archived ones only on request.
    Page list(const std::string& owner, const std::string& q, bool include_archived, long limit, long offset) {
        Page out;
        Database::get().execute_read([&](auto& txn) {
            const std::string where =
                "owner_id = $1::uuid AND ($2 = '' OR name ILIKE '%' || $2 || '%' ESCAPE '\\' "
                " OR brand ILIKE '%' || $2 || '%' ESCAPE '\\') "
                "AND ($3::boolean OR NOT archived)";
            auto rows = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + kColumns +
                                            " FROM food_items WHERE " + where +
                                            " ORDER BY lower(name), id LIMIT $4 OFFSET $5) t",
                                        owner,
                                        detail::escape_like(q),
                                        include_archived,
                                        limit,
                                        offset);
            out.rows = nlohmann::json::parse(rows[0][0].template as<std::string>());
            auto total = txn.exec_params(
                "SELECT COUNT(*) FROM food_items WHERE " + where, owner, detail::escape_like(q), include_archived);
            out.total = total[0][0].template as<long>();
            return 0;
        });
        return out;
    }

    /// The items used most recently in the diary, each once, newest use first.
    nlohmann::json recent(const std::string& owner, long limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM ("
                " SELECT " +
                    kColumns +
                    " FROM food_items i JOIN ("
                    "  SELECT item_id, MAX(logged_at) AS last_used FROM food_entries "
                    "  WHERE owner_id = $1::uuid AND item_id IS NOT NULL GROUP BY item_id) u "
                    "  ON u.item_id = i.id "
                    " WHERE i.owner_id = $1::uuid AND NOT i.archived "
                    " ORDER BY u.last_used DESC LIMIT $2) t",
                owner,
                limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + kColumns +
                                         " FROM food_items WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                     owner,
                                     id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    nlohmann::json create(const std::string& owner, const Input& in) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO food_items (owner_id, source, off_code, name, brand, per, kcal, protein_g, fat_g, "
                " carbs_g, fiber_g, sugar_g, salt_g, servings) "
                "VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14::jsonb) "
                "RETURNING id::text",
                owner,
                in.source,
                in.off_code,
                in.name,
                in.brand,
                in.per,
                in.kcal,
                in.protein_g,
                in.fat_g,
                in.carbs_g,
                in.fiber_g,
                in.sugar_g,
                in.salt_g,
                in.servings_json);
            return *find_in(txn, owner, r[0][0].template as<std::string>());
        });
    }

    /**
     * @brief The user's copy of an Open Food Facts product: the existing row
     *        for this barcode, or a new one from @p in (source off).
     * @returns the item and whether it was created now.
     */
    std::pair<nlohmann::json, bool> upsert_off(const std::string& owner, const Input& in) {
        return Database::get().execute_write([&](auto& txn) -> std::pair<nlohmann::json, bool> {
            // Two parallel copies of one barcode: the unique index decides, and
            // the loser reads the winner's row instead of answering 500.
            auto r = txn.exec_params(
                "INSERT INTO food_items (owner_id, source, off_code, name, brand, per, kcal, protein_g, fat_g, "
                " carbs_g, fiber_g, sugar_g, salt_g, servings) "
                "VALUES ($1::uuid, 'off', $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13::jsonb) "
                "ON CONFLICT (owner_id, off_code) WHERE off_code IS NOT NULL DO NOTHING "
                "RETURNING id::text",
                owner,
                in.off_code,
                in.name,
                in.brand,
                in.per,
                in.kcal,
                in.protein_g,
                in.fat_g,
                in.carbs_g,
                in.fiber_g,
                in.sugar_g,
                in.salt_g,
                in.servings_json);
            if (!r.empty()) {
                return {*find_in(txn, owner, r[0][0].template as<std::string>()), true};
            }
            auto existing = txn.exec_params(
                "SELECT id::text FROM food_items WHERE owner_id = $1::uuid AND off_code = $2", owner, in.off_code);
            return {*find_in(txn, owner, existing[0][0].template as<std::string>()), false};
        });
    }

    /// The items the parse prompt gets: the ones touched last, so a user with
    /// many products still has the current ones in front of the model.
    nlohmann::json for_prompt(const std::string& owner, long limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto rows = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT id::text AS id, name, brand, per, kcal, "
                " protein_g, fat_g, carbs_g FROM food_items WHERE owner_id = $1::uuid AND NOT archived "
                " ORDER BY updated_at DESC, id LIMIT $2) t",
                owner,
                limit);
            return nlohmann::json::parse(rows[0][0].template as<std::string>());
        });
    }

    /// @throws FoodItemNotFound.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return Database::get().execute_write([&](auto& txn) {
            // The nullable nutrients take a flag each: "set to NULL" differs from "leave".
            auto r = txn.exec_params(
                "UPDATE food_items SET "
                " name = COALESCE($3, name), brand = COALESCE($4, brand), per = COALESCE($5, per), "
                " kcal = COALESCE($6, kcal), protein_g = COALESCE($7, protein_g), fat_g = COALESCE($8, fat_g), "
                " carbs_g = COALESCE($9, carbs_g), "
                " fiber_g = CASE WHEN $10::boolean THEN $11 ELSE fiber_g END, "
                " sugar_g = CASE WHEN $12::boolean THEN $13 ELSE sugar_g END, "
                " salt_g = CASE WHEN $14::boolean THEN $15 ELSE salt_g END, "
                " servings = COALESCE($16::jsonb, servings), archived = COALESCE($17, archived), "
                " updated_at = now() "
                "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id::text",
                owner,
                id,
                p.name,
                p.brand,
                p.per,
                p.kcal,
                p.protein_g,
                p.fat_g,
                p.carbs_g,
                p.fiber_g.has_value(),
                p.fiber_g.has_value() ? *p.fiber_g : std::optional<double>{},
                p.sugar_g.has_value(),
                p.sugar_g.has_value() ? *p.sugar_g : std::optional<double>{},
                p.salt_g.has_value(),
                p.salt_g.has_value() ? *p.salt_g : std::optional<double>{},
                p.servings_json,
                p.archived);
            if (r.empty()) {
                throw FoodItemNotFound();
            }
            return *find_in(txn, owner, id);
        });
    }

    /// Delete an item nothing references; archive one that entries use.
    /// @returns "deleted" or "archived". @throws FoodItemNotFound.
    std::string remove(const std::string& owner, const std::string& id) {
        return Database::get().execute_write([&](auto& txn) -> std::string {
            auto used = txn.exec_params(
                "SELECT EXISTS (SELECT 1 FROM food_entries WHERE item_id = $2::uuid) "
                "FROM food_items WHERE owner_id = $1::uuid AND id = $2::uuid",
                owner,
                id);
            if (used.empty()) {
                throw FoodItemNotFound();
            }
            if (used[0][0].template as<bool>()) {
                txn.exec_params("UPDATE food_items SET archived = true, updated_at = now() WHERE id = $1::uuid", id);
                return "archived";
            }
            txn.exec_params("DELETE FROM food_items WHERE id = $1::uuid", id);
            return "deleted";
        });
    }

    static constexpr const char* kColumnsRaw =
        "id, source, off_code, name, brand, per, kcal, protein_g, fat_g, carbs_g, fiber_g, sugar_g, salt_g, "
        "servings, archived, "
        "to_char(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS created_at, "
        "to_char(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS updated_at";

private:
    inline static const std::string kColumns = kColumnsRaw;

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + kColumns +
                                     " FROM food_items WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories
