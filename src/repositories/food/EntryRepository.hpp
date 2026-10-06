/**
 * @file EntryRepository.hpp
 * @brief The diary of the food module (table food_entries).
 *
 * An entry carries its own numbers, copied from the item and the grams when
 * it is written. Changing the grams of an entry that still has its item
 * recomputes them; a quick entry (no item) keeps what the user typed.
 */

#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories {

struct FoodEntryNotFound : NotFoundError {
    FoodEntryNotFound() : NotFoundError("food_entry") {}
};

struct InvalidFoodDate : ValidationError {
    InvalidFoodDate() : ValidationError("invalid_date", "not a valid date") {}
};

class EntryRepository {
public:
    struct Input {
        std::string date;
        std::string meal;
        std::string name;
        std::optional<std::string> item_id;
        std::optional<double> grams;
        double kcal = 0;
        double protein_g = 0;
        double fat_g = 0;
        double carbs_g = 0;
        std::optional<double> fiber_g;
        std::optional<double> sugar_g;
        std::optional<double> salt_g;
        bool estimated = false;
        std::string note;
    };

    struct Patch {
        std::optional<std::string> date;
        std::optional<std::string> meal;
        std::optional<double> grams;
        std::optional<std::string> note;
    };

    /// The numbers of @p grams of the owner's item, or nullopt when the item is not theirs.
    struct FromItem {
        std::string name;
        double kcal, protein_g, fat_g, carbs_g;
        std::optional<double> fiber_g, sugar_g, salt_g;
    };
    std::optional<FromItem> from_item(const std::string& owner, const std::string& item_id, double grams) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<FromItem> { return from_item_in(txn, owner, item_id, grams); });
    }

    /// {date, totals, meals: {breakfast: [...], lunch, dinner, snack}}.
    nlohmann::json day(const std::string& owner, const std::string& date) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_read([&](auto& txn) {
                    auto r = txn.exec_params(
                        "SELECT json_build_object("
                        " 'date', $2::date::text, "
                        " 'totals', (SELECT json_build_object('kcal', COALESCE(SUM(kcal), 0), "
                        "    'protein_g', COALESCE(SUM(protein_g), 0), 'fat_g', COALESCE(SUM(fat_g), 0), "
                        "    'carbs_g', COALESCE(SUM(carbs_g), 0), 'fiber_g', SUM(fiber_g), 'sugar_g', SUM(sugar_g), "
                        "    'salt_g', SUM(salt_g), 'entries', COUNT(*)) "
                        "   FROM food_entries WHERE owner_id = $1::uuid AND date = $2::date), "
                        " 'meals', (SELECT json_object_agg(m.meal, COALESCE(e.rows, '[]'::json)) "
                        "   FROM (VALUES ('breakfast'), ('lunch'), ('dinner'), ('snack')) m(meal) "
                        "   LEFT JOIN (SELECT meal, json_agg(t ORDER BY position, logged_at) AS rows FROM (SELECT " +
                            kColumns +
                            " FROM food_entries WHERE owner_id = $1::uuid AND date = $2::date) t GROUP BY meal) e "
                            "   ON e.meal = m.meal))",
                        owner,
                        date);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &translate);
    }

    /// Seven days from @p from: [{date, kcal, protein_g, fat_g, carbs_g, entries}], zeros for empty days.
    nlohmann::json week(const std::string& owner, const std::string& from) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_read([&](auto& txn) {
                    auto r = txn.exec_params(
                        "SELECT json_agg(json_build_object('date', d::date::text, "
                        " 'kcal', COALESCE(s.kcal, 0), 'protein_g', COALESCE(s.protein_g, 0), "
                        " 'fat_g', COALESCE(s.fat_g, 0), 'carbs_g', COALESCE(s.carbs_g, 0), "
                        " 'entries', COALESCE(s.n, 0)) ORDER BY d) "
                        "FROM generate_series($2::date, $2::date + 6, interval '1 day') d "
                        "LEFT JOIN (SELECT date, SUM(kcal) AS kcal, SUM(protein_g) AS protein_g, SUM(fat_g) AS fat_g, "
                        "   SUM(carbs_g) AS carbs_g, COUNT(*) AS n FROM food_entries "
                        "   WHERE owner_id = $1::uuid AND date BETWEEN $2::date AND $2::date + 6 GROUP BY date) s "
                        " ON s.date = d::date",
                        owner,
                        from);
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &translate);
    }

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write(
                    [&](auto& txn) { return *find_in(txn, owner, insert_in(txn, owner, in)); });
            },
            &translate);
    }

    /// All in one transaction: a refused line writes nothing.
    nlohmann::json create_many(const std::string& owner, const std::vector<Input>& inputs) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    nlohmann::json out = nlohmann::json::array();
                    for (const auto& in : inputs) {
                        out.push_back(*find_in(txn, owner, insert_in(txn, owner, in)));
                    }
                    return out;
                });
            },
            &translate);
    }

    /// @throws FoodEntryNotFound, InvalidFoodDate.
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE food_entries SET date = COALESCE($3::date, date), meal = COALESCE($4, meal), "
                        " grams = COALESCE($5, grams), note = COALESCE($6, note) "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING item_id::text, grams",
                        owner,
                        id,
                        p.date,
                        p.meal,
                        p.grams,
                        p.note);
                    if (r.empty()) {
                        throw FoodEntryNotFound();
                    }
                    // New grams of an entry that still has its item: numbers follow the item.
                    if (p.grams.has_value() && !r[0][0].is_null()) {
                        const auto n = from_item_in(txn, owner, r[0][0].template as<std::string>(), *p.grams);
                        if (n.has_value()) {
                            txn.exec_params(
                                "UPDATE food_entries SET kcal = $2, protein_g = $3, fat_g = $4, carbs_g = $5, "
                                " fiber_g = $6, sugar_g = $7, salt_g = $8, estimated = false WHERE id = $1::uuid",
                                id,
                                n->kcal,
                                n->protein_g,
                                n->fat_g,
                                n->carbs_g,
                                n->fiber_g,
                                n->sugar_g,
                                n->salt_g);
                        }
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &translate);
    }

    /// @throws FoodEntryNotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM food_entries WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw FoodEntryNotFound();
            }
            return 0;
        });
    }

private:
    inline static const std::string kColumns =
        "id, date::text, meal, item_id, name, grams, kcal, protein_g, fat_g, carbs_g, fiber_g, sugar_g, salt_g, "
        "estimated, note, position, "
        "to_char(logged_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"') AS logged_at";

    /// A malformed date reaches Postgres as 22007/22008.
    static void translate(std::string_view sqlstate) {
        if (sqlstate == "22007" || sqlstate == "22008") {
            throw InvalidFoodDate();
        }
    }

    static double scaled(double per_100, double grams) { return std::round(per_100 * grams / 100.0 * 10.0) / 10.0; }

    template <typename Txn>
    static std::optional<FromItem> from_item_in(Txn& txn,
                                                const std::string& owner,
                                                const std::string& item_id,
                                                double grams) {
        auto r = txn.exec_params(
            "SELECT name, kcal, protein_g, fat_g, carbs_g, fiber_g, sugar_g, salt_g FROM food_items "
            "WHERE owner_id = $1::uuid AND id = $2::uuid",
            owner,
            item_id);
        if (r.empty()) {
            return std::nullopt;
        }
        const auto& row = r[0];
        const auto opt = [&](int i) -> std::optional<double> {
            if (row[i].is_null()) {
                return std::nullopt;
            }
            return scaled(row[i].template as<double>(), grams);
        };
        return FromItem{row[0].template as<std::string>(),
                        scaled(row[1].template as<double>(), grams),
                        scaled(row[2].template as<double>(), grams),
                        scaled(row[3].template as<double>(), grams),
                        scaled(row[4].template as<double>(), grams),
                        opt(5),
                        opt(6),
                        opt(7)};
    }

    template <typename Txn>
    static std::string insert_in(Txn& txn, const std::string& owner, const Input& in) {
        auto r = txn.exec_params(
            "INSERT INTO food_entries (owner_id, date, meal, item_id, name, grams, kcal, protein_g, fat_g, carbs_g, "
            " fiber_g, sugar_g, salt_g, estimated, note, position) "
            "VALUES ($1::uuid, $2::date, $3, $4::uuid, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, "
            " COALESCE((SELECT MAX(position) FROM food_entries WHERE owner_id = $1::uuid AND date = $2::date "
            "   AND meal = $3), 0) + 1) "
            "RETURNING id::text",
            owner,
            in.date,
            in.meal,
            in.item_id,
            in.name,
            in.grams,
            in.kcal,
            in.protein_g,
            in.fat_g,
            in.carbs_g,
            in.fiber_g,
            in.sugar_g,
            in.salt_g,
            in.estimated,
            in.note);
        return r[0][0].template as<std::string>();
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + kColumns +
                                     " FROM food_entries WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories
