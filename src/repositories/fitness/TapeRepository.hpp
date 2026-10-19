/**
 * @file TapeRepository.hpp
 * @brief tape_measurements: girths the owner measured by hand (waist, chest,
 *        hips, neck, arm, thigh, in cm) on a date, with a note. Unlike the Mi
 *        tables beside it, the rows belong to the app user (owner_id), and
 *        every statement filters by owner.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"
#include "repositories/SqlTime.hpp"

namespace Repositories::Tape {

struct MeasurementNotFound : NotFoundError {
    MeasurementNotFound() : NotFoundError("measurement") {}
};

struct MeasurementInvariant : ValidationError {
    MeasurementInvariant()
        : ValidationError("invariant", "a measurement needs a date and at least one girth from 0 to 400 cm") {}
};

class TapeRepository {
public:
    /// The girths, in the order of the columns and the API fields.
    static constexpr std::array<std::string_view, 6> kGirths = {
        "waist_cm", "chest_cm", "hips_cm", "neck_cm", "arm_cm", "thigh_cm"};

    struct Input {
        std::string date;
        std::array<std::optional<double>, 6> girths;  // by kGirths
        std::string note;
    };

    /// A partial edit: a girth given as null clears it.
    struct Patch {
        std::optional<std::string> date;
        std::array<std::optional<std::optional<double>>, 6> girths;
        std::optional<std::string> note;
    };

    struct Filter {
        std::string from;  // "" open
        std::string to;    // "" open
        int limit = 50;
        int offset = 0;
    };

    /// @throws MeasurementInvariant.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO tape_measurements (owner_id, date, waist_cm, chest_cm, hips_cm, neck_cm, arm_cm, "
                        " thigh_cm, note) VALUES ($1::uuid, $2::date, $3, $4, $5, $6, $7, $8, $9) RETURNING id::text",
                        owner,
                        in.date,
                        in.girths[0],
                        in.girths[1],
                        in.girths[2],
                        in.girths[3],
                        in.girths[4],
                        in.girths[5],
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &translate);
    }

    /// @throws MeasurementNotFound, MeasurementInvariant (an edit that clears every girth).
    nlohmann::json update(const std::string& owner, const std::string& id, const Patch& p) {
        const auto set = [&](std::size_t i) { return p.girths[i].has_value(); };
        const auto val = [&](std::size_t i) { return p.girths[i].has_value() ? *p.girths[i] : std::nullopt; };
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE tape_measurements SET date = COALESCE($3::date, date), note = COALESCE($4, note), "
                        " waist_cm = CASE WHEN $5::boolean THEN $6::numeric ELSE waist_cm END, "
                        " chest_cm = CASE WHEN $7::boolean THEN $8::numeric ELSE chest_cm END, "
                        " hips_cm = CASE WHEN $9::boolean THEN $10::numeric ELSE hips_cm END, "
                        " neck_cm = CASE WHEN $11::boolean THEN $12::numeric ELSE neck_cm END, "
                        " arm_cm = CASE WHEN $13::boolean THEN $14::numeric ELSE arm_cm END, "
                        " thigh_cm = CASE WHEN $15::boolean THEN $16::numeric ELSE thigh_cm END "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        p.date,
                        p.note,
                        set(0),
                        val(0),
                        set(1),
                        val(1),
                        set(2),
                        val(2),
                        set(3),
                        val(3),
                        set(4),
                        val(4),
                        set(5),
                        val(5));
                    if (r.empty()) {
                        throw MeasurementNotFound();
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &translate);
    }

    /// @throws MeasurementNotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            if (txn.exec_params(
                       "DELETE FROM tape_measurements WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                       owner,
                       id)
                    .empty()) {
                throw MeasurementNotFound();
            }
            return true;
        });
    }

    /// {data: newest first, total}.
    nlohmann::json list(const std::string& owner, const Filter& f) {
        return Database::get().execute_read([&](auto& txn) {
            const std::string where =
                " WHERE owner_id = $1::uuid AND ($2 = '' OR date >= NULLIF($2, '')::date) "
                " AND ($3 = '' OR date <= NULLIF($3, '')::date) ";
            auto total =
                txn.exec_params("SELECT count(*) FROM tape_measurements" + where + " AND $4::int >= 0 AND $5::int >= 0",
                                owner,
                                f.from,
                                f.to,
                                f.limit,
                                f.offset);
            auto r = txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json)::text FROM (SELECT " +
                                         std::string(kColumns) + " FROM tape_measurements" + where +
                                         " ORDER BY date DESC, created_at DESC LIMIT $4 OFFSET $5) t",
                                     owner,
                                     f.from,
                                     f.to,
                                     f.limit,
                                     f.offset);
            return nlohmann::json{{"data", nlohmann::json::parse(r[0][0].template as<std::string>())},
                                  {"total", total[0][0].template as<long>()}};
        });
    }

private:
    static constexpr const char* kColumns =
        "id, date::text AS date, waist_cm::double precision AS waist_cm, chest_cm::double precision AS chest_cm, "
        " hips_cm::double precision AS hips_cm, neck_cm::double precision AS neck_cm, "
        " arm_cm::double precision AS arm_cm, thigh_cm::double precision AS thigh_cm, note, " SQL_ISO_UTC(
            "created_at") " AS created_at, " SQL_ISO_UTC("updated_at") " AS updated_at";

    static void translate(std::string_view sqlstate) {
        if (sqlstate == "23514") {  // check_violation
            throw MeasurementInvariant();
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        auto r = txn.exec_params("SELECT row_to_json(t)::text FROM (SELECT " + std::string(kColumns) +
                                     " FROM tape_measurements WHERE owner_id = $1::uuid AND id = $2::uuid) t",
                                 owner,
                                 id);
        if (r.empty()) {
            return std::nullopt;
        }
        return nlohmann::json::parse(r[0][0].template as<std::string>());
    }
};

}  // namespace Repositories::Tape
