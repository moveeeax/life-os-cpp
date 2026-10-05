/**
 * @file ExerciseRepository.hpp
 * @brief Exercise library of the workout module: the seeded library rows
 *        (source = 'library', no owner) and the owner's custom exercises.
 *
 * Rows leave as JSON built by Postgres, like the fitness read repository:
 * the columns of kColumns are the field names of the API.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"

namespace Repositories {

struct ExerciseNotFound : NotFoundError {
    ExerciseNotFound() : NotFoundError("exercise") {}
};

class ExerciseRepository {
public:
    struct Page {
        nlohmann::json rows = nlohmann::json::array();
        long total = 0;
    };

    /// Empty string = no filter on that field.
    struct Filter {
        std::string q;
        std::string muscle;
        std::string equipment;
        std::string category;
        std::string source;
        bool include_archived = false;
    };

    /// Fields of a new custom exercise. The three lists are JSON arrays of
    /// strings, serialized; Postgres unpacks them into text[].
    struct NewExercise {
        std::string name;
        std::string category;
        std::string equipment;
        std::string primary_muscles_json = "[]";
        std::string secondary_muscles_json = "[]";
        std::string instructions_json = "[]";
        std::string tracking_mode;
    };

    /// A partial update: nullopt leaves the column as it is.
    struct Patch {
        std::optional<std::string> name;
        std::optional<std::string> category;
        std::optional<std::string> equipment;
        std::optional<std::string> primary_muscles_json;
        std::optional<std::string> secondary_muscles_json;
        std::optional<std::string> instructions_json;
        std::optional<std::string> tracking_mode;
        std::optional<bool> archived;

        /// True when the patch touches a column a library row keeps fixed.
        bool touches_content() const {
            return name || category || equipment || primary_muscles_json || secondary_muscles_json || instructions_json;
        }
    };

    /// Library exercises plus the owner's own, filtered and paged by name.
    Page list(const std::string& owner, const Filter& f, long limit, long offset) {
        Page out;
        Database::get().execute_read([&](auto& txn) {
            auto agg =
                txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + std::string(kColumns) +
                                    " FROM exercises e " + kWhere + " ORDER BY e.name, e.id LIMIT $8 OFFSET $9) t",
                                owner,
                                f.q,
                                f.muscle,
                                f.equipment,
                                f.category,
                                f.source,
                                f.include_archived,
                                limit,
                                offset);
            out.rows = nlohmann::json::parse(agg[0][0].template as<std::string>());
            auto total = txn.exec_params("SELECT COUNT(*) FROM exercises e " + std::string(kWhere),
                                         owner,
                                         f.q,
                                         f.muscle,
                                         f.equipment,
                                         f.category,
                                         f.source,
                                         f.include_archived);
            out.total = total[0][0].template as<long>();
            return 0;
        });
        return out;
    }

    /// One exercise the owner may see, or nullopt.
    std::optional<nlohmann::json> find(const std::string& owner, const std::string& id) {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<nlohmann::json> { return find_in(txn, owner, id); });
    }

    /// Insert a custom exercise and return it.
    nlohmann::json create(const std::string& owner, const NewExercise& e) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO exercises (id, source, owner_id, name, category, equipment, primary_muscles, "
                "secondary_muscles, instructions, tracking_mode) "
                "VALUES ('custom_' || replace(gen_random_uuid()::text, '-', ''), 'custom', $1::uuid, $2, $3, "
                "NULLIF($4, ''), ARRAY(SELECT json_array_elements_text($5::json)), "
                "ARRAY(SELECT json_array_elements_text($6::json)), "
                "ARRAY(SELECT json_array_elements_text($7::json)), $8) RETURNING id",
                owner,
                e.name,
                e.category,
                e.equipment,
                e.primary_muscles_json,
                e.secondary_muscles_json,
                e.instructions_json,
                e.tracking_mode);
            return *find_in(txn, owner, r[0][0].template as<std::string>());
        });
    }

    /**
     * @brief Apply a patch. A library row is shared by every user, so it
     *        changes only when @p may_edit_library is set (administrators),
     *        and then only in tracking_mode and archived. Anything else on a
     *        library row is a ValidationError.
     * @throws ExerciseNotFound when the exercise does not exist or belongs to
     *         someone else.
     */
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p, bool may_edit_library) {
        return Database::get().execute_write([&](auto& txn) {
            auto src = txn.exec_params(
                "SELECT source FROM exercises WHERE id = $2 AND (source = 'library' OR owner_id = $1::uuid)",
                owner,
                id);
            if (src.empty()) {
                throw ExerciseNotFound();
            }
            const bool is_library = src[0][0].template as<std::string>() == "library";
            if (is_library && !may_edit_library) {
                throw ValidationError("library_exercise_read_only",
                                      "only an administrator can change a library exercise");
            }
            if (is_library && p.touches_content()) {
                throw ValidationError("library_exercise_read_only",
                                      "only tracking_mode and archived can be changed on a library exercise");
            }
            txn.exec_params(
                "UPDATE exercises SET "
                "name = COALESCE($3, name), category = COALESCE($4, category), "
                "equipment = CASE WHEN $5::text IS NULL THEN equipment ELSE NULLIF($5, '') END, "
                "primary_muscles = CASE WHEN $6::text IS NULL THEN primary_muscles "
                "  ELSE ARRAY(SELECT json_array_elements_text($6::json)) END, "
                "secondary_muscles = CASE WHEN $7::text IS NULL THEN secondary_muscles "
                "  ELSE ARRAY(SELECT json_array_elements_text($7::json)) END, "
                "instructions = CASE WHEN $8::text IS NULL THEN instructions "
                "  ELSE ARRAY(SELECT json_array_elements_text($8::json)) END, "
                "tracking_mode = COALESCE($9, tracking_mode), archived = COALESCE($10, archived) "
                "WHERE id = $2 AND (source = 'library' OR owner_id = $1::uuid)",
                owner,
                id,
                p.name,
                p.category,
                p.equipment,
                p.primary_muscles_json,
                p.secondary_muscles_json,
                p.instructions_json,
                p.tracking_mode,
                p.archived);
            return *find_in(txn, owner, id);
        });
    }

private:
    static constexpr const char* kColumns =
        "e.id, e.source, e.name, e.category, e.equipment, e.level, e.force, e.mechanic, "
        "e.primary_muscles, e.secondary_muscles, e.instructions, e.images, e.tracking_mode, e.archived";

    // $1 owner, $2 q, $3 muscle, $4 equipment, $5 category, $6 source, $7 include_archived.
    static constexpr const char* kWhere =
        "WHERE (e.source = 'library' OR e.owner_id = $1::uuid) "
        "AND ($2 = '' OR e.name ILIKE '%' || $2 || '%') "
        "AND ($3 = '' OR $3 = ANY(e.primary_muscles) OR $3 = ANY(e.secondary_muscles)) "
        "AND ($4 = '' OR e.equipment = $4) "
        "AND ($5 = '' OR e.category = $5) "
        "AND ($6 = '' OR e.source = $6) "
        "AND ($7::boolean OR NOT e.archived)";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t) FROM (SELECT " + std::string(kColumns) +
                                     " FROM exercises e WHERE e.id = $2 "
                                     "AND (e.source = 'library' OR e.owner_id = $1::uuid)) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories
