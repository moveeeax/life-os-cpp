/**
 * @file MedicalRepository.hpp
 * @brief The family medical archive (migration 051): people, doctors and
 *        clinics, visits, records and medications. Every query names the
 *        owner, and a reference to a person, a doctor or a visit must be the
 *        owner's too: a foreign id reads as unknown, never as a link. A visit
 *        with a follow-up date and a medication with an end date can open a
 *        task through TaskRepository, in the same transaction that links it.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"
#include "repositories/tasks/TaskRepository.hpp"
#include "utils/Utf8.hpp"

namespace Repositories::Medical {

struct NotFound : NotFoundError {
    explicit NotFound(const char* what) : NotFoundError(what) {}
};

struct Invariant : ValidationError {
    Invariant() : ValidationError("invariant", "the row breaks a rule of the medical archive") {}
};

struct InvalidDate : ValidationError {
    InvalidDate() : ValidationError("invalid_date", "not a valid date") {}
};

/// A person, doctor or visit id that is not the caller's (or not there).
struct UnknownReference : ValidationError {
    explicit UnknownReference(const std::string& field)
        : ValidationError("unknown_reference", field + " is not one of yours") {}
};

struct PersonInUse : ConflictError {
    PersonInUse()
        : ConflictError("person_in_use", "the person still has visits, records or medications; delete those first") {}
};

/// The visit has no follow-up date, or the medication no end date, to put a task on.
struct NoDate : ConflictError {
    explicit NoDate(const char* what) : ConflictError("no_date", std::string(what) + " has no date to put a task on") {}
};

namespace detail {

using Repositories::detail::translate_sql;

inline void translate(std::string_view sqlstate) {
    if (sqlstate == "23514") {  // check_violation
        throw Invariant();
    }
    if (sqlstate == "22008" || sqlstate == "22007") {  // datetime_field_overflow, invalid_datetime_format
        throw InvalidDate();
    }
}

/// One JSON object (row_to_json) or nullopt.
template <typename Txn, typename... Args>
std::optional<nlohmann::json> one(Txn& txn, const std::string& sql, Args&&... args) {
    auto r = txn.exec_params("SELECT row_to_json(t) FROM (" + sql + ") t", std::forward<Args>(args)...);
    if (r.empty()) {
        return std::nullopt;
    }
    return nlohmann::json::parse(r[0][0].template as<std::string>());
}

/// A JSON array (json_agg) of the rows of @p sql, in its order.
template <typename Txn, typename... Args>
nlohmann::json many(Txn& txn, const std::string& sql, Args&&... args) {
    auto r =
        txn.exec_params("SELECT COALESCE(json_agg(t), '[]'::json) FROM (" + sql + ") t", std::forward<Args>(args)...);
    return nlohmann::json::parse(r[0][0].template as<std::string>());
}

template <typename Txn>
bool owns(Txn& txn, const char* table, const std::string& owner, const std::optional<std::string>& id) {
    if (!id) {
        return true;
    }
    return !txn.exec_params(
                   std::string("SELECT 1 FROM ") + table + " WHERE owner_id = $1::uuid AND id = $2::uuid", owner, *id)
                .empty();
}

/// A task title of at most 200 characters (task_items' CHECK).
inline std::string task_title(const std::string& text) {
    return Utils::Utf8::cut(text, 200);
}

/// The task, when its id is set and the task is still there.
template <typename Txn>
std::optional<nlohmann::json> existing_task(Txn& txn, const std::string& owner, const nlohmann::json& id) {
    if (!id.is_string()) {
        return std::nullopt;
    }
    return one(txn,
               "SELECT id::text AS id, title, to_char(due, 'YYYY-MM-DD') AS due, status FROM task_items "
               " WHERE owner_id = $1::uuid AND id = $2::uuid",
               owner,
               id.get<std::string>());
}

/// Creates the task of a date and answers it the way existing_task does.
template <typename Txn>
nlohmann::json new_task(
    Txn& txn, const std::string& owner, const std::string& title, const std::string& due, const std::string& note) {
    Repositories::Tasks::TaskRepository::Input in;
    in.title = task_title(title);
    in.area = "health";
    in.due = due;
    in.note = Utils::Utf8::cut(note, 2000);
    const std::string id = Repositories::Tasks::TaskRepository::create_in(txn, owner, in);
    return *existing_task(txn, owner, nlohmann::json(id));
}

}  // namespace detail

// ── people ──────────────────────────────────────────────────────────────────

class PersonRepository {
public:
    struct Input {
        std::string name;
        std::string relation;
        std::string note;
    };

    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return detail::many(txn,
                                std::string("SELECT ") + kColumns +
                                    " FROM medical_people WHERE owner_id = $1::uuid ORDER BY name, created_at, id",
                                owner);
        });
    }

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO medical_people (owner_id, name, relation, note) VALUES ($1::uuid, $2, $3, $4) "
                        "RETURNING id::text",
                        owner,
                        in.name,
                        in.relation,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE medical_people SET name = $3, relation = $4, note = $5 "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.name,
                        in.relation,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("person");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// @throws NotFound, PersonInUse (the archive of a person is never dropped by one click).
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            if (!detail::owns(txn, "medical_people", owner, id)) {
                throw NotFound("person");
            }
            auto used = txn.exec_params(
                "SELECT EXISTS (SELECT 1 FROM medical_visits WHERE owner_id = $1::uuid AND person_id = $2::uuid) "
                " OR EXISTS (SELECT 1 FROM medical_records WHERE owner_id = $1::uuid AND person_id = $2::uuid) "
                " OR EXISTS (SELECT 1 FROM medical_medications WHERE owner_id = $1::uuid AND person_id = $2::uuid)",
                owner,
                id);
            if (used[0][0].template as<bool>()) {
                throw PersonInUse();
            }
            txn.exec_params("DELETE FROM medical_people WHERE owner_id = $1::uuid AND id = $2::uuid", owner, id);
            return true;
        });
    }

private:
    static constexpr const char* kColumns = "id::text AS id, name, relation, note";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        return detail::one(
            txn,
            std::string("SELECT ") + kColumns + " FROM medical_people WHERE owner_id = $1::uuid AND id = $2::uuid",
            owner,
            id);
    }
};

// ── doctors and clinics ─────────────────────────────────────────────────────

class ProviderRepository {
public:
    struct Input {
        std::string kind;  // doctor | clinic
        std::string name;
        std::string specialty;
        std::string clinic;
        std::string phone;
        std::string email;
        std::string address;
        std::string note;
    };

    nlohmann::json list(const std::string& owner) {
        return Database::get().execute_read([&](auto& txn) {
            return detail::many(txn,
                                std::string("SELECT ") + kColumns +
                                    " FROM medical_providers WHERE owner_id = $1::uuid ORDER BY name, created_at, id",
                                owner);
        });
    }

    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO medical_providers (owner_id, kind, name, specialty, clinic, phone, email, "
                        "address, "
                        " note) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7, $8, $9) RETURNING id::text",
                        owner,
                        in.kind,
                        in.name,
                        in.specialty,
                        in.clinic,
                        in.phone,
                        in.email,
                        in.address,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @throws NotFound.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE medical_providers SET kind = $3, name = $4, specialty = $5, clinic = $6, phone = $7, "
                        " email = $8, address = $9, note = $10 WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING "
                        "id",
                        owner,
                        id,
                        in.kind,
                        in.name,
                        in.specialty,
                        in.clinic,
                        in.phone,
                        in.email,
                        in.address,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("provider");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Visits keep their date and reason; their doctor becomes empty. @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM medical_providers WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("provider");
            }
            return true;
        });
    }

private:
    static constexpr const char* kColumns =
        "id::text AS id, kind, name, specialty, clinic, phone, email, address, note";

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        return detail::one(
            txn,
            std::string("SELECT ") + kColumns + " FROM medical_providers WHERE owner_id = $1::uuid AND id = $2::uuid",
            owner,
            id);
    }
};

// ── visits ──────────────────────────────────────────────────────────────────

class VisitRepository {
public:
    struct Input {
        std::string person_id;
        std::optional<std::string> provider_id;
        std::string date;
        std::string reason;
        std::string outcome;
        std::optional<std::string> follow_up;
    };

    /// Newest first; @p person "" means everyone.
    nlohmann::json list(const std::string& owner, const std::string& person) {
        return Database::get().execute_read([&](auto& txn) {
            return detail::many(txn,
                                std::string("SELECT ") + kColumns + kFrom +
                                    " WHERE v.owner_id = $1::uuid AND ($2 = '' OR v.person_id::text = $2) "
                                    " ORDER BY v.date DESC, v.created_at DESC, v.id",
                                owner,
                                person);
        });
    }

    /// @throws UnknownReference.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_refs(txn, owner, in);
                    auto r = txn.exec_params(
                        "INSERT INTO medical_visits (owner_id, person_id, provider_id, date, reason, outcome, "
                        " follow_up) VALUES ($1::uuid, $2::uuid, $3::uuid, $4::date, $5, $6, $7::date) "
                        "RETURNING id::text",
                        owner,
                        in.person_id,
                        in.provider_id,
                        in.date,
                        in.reason,
                        in.outcome,
                        in.follow_up);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @throws NotFound, UnknownReference.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_refs(txn, owner, in);
                    auto r = txn.exec_params(
                        "UPDATE medical_visits SET person_id = $3::uuid, provider_id = $4::uuid, date = $5::date, "
                        " reason = $6, outcome = $7, follow_up = $8::date "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.person_id,
                        in.provider_id,
                        in.date,
                        in.reason,
                        in.outcome,
                        in.follow_up);
                    if (r.empty()) {
                        throw NotFound("visit");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Records of the visit keep their date; their link to it goes. @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM medical_visits WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("visit");
            }
            return true;
        });
    }

    struct TaskAnswer {
        nlohmann::json visit;
        nlohmann::json task;
        bool created = false;
    };

    /// The task of the follow-up date: the linked one while it exists, else a new open
    /// task in area health due that day. @throws NotFound, NoDate.
    TaskAnswer follow_up_task(const std::string& owner, const std::string& id) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    std::optional<nlohmann::json> row =
                        detail::one(txn,
                                    "SELECT v.id::text AS id, v.reason, to_char(v.date, 'YYYY-MM-DD') AS date, "
                                    " to_char(v.follow_up, 'YYYY-MM-DD') AS follow_up, "
                                    " v.follow_up_task_id::text AS task_id, p.name AS person "
                                    " FROM medical_visits v JOIN medical_people p ON p.id = v.person_id "
                                    " WHERE v.owner_id = $1::uuid AND v.id = $2::uuid FOR UPDATE OF v",
                                    owner,
                                    id);
                    if (!row) {
                        throw NotFound("visit");
                    }
                    if (!(*row)["follow_up"].is_string()) {
                        throw NoDate("the visit");
                    }
                    TaskAnswer out;
                    if (auto task = detail::existing_task(txn, owner, (*row)["task_id"])) {
                        out.task = *task;
                    } else {
                        out.task = detail::new_task(
                            txn,
                            owner,
                            (*row)["person"].get<std::string>() + ": follow-up, " + (*row)["reason"].get<std::string>(),
                            (*row)["follow_up"].get<std::string>(),
                            "From the visit of " + (*row)["date"].get<std::string>() + ".");
                        txn.exec_params(
                            "UPDATE medical_visits SET follow_up_task_id = $3::uuid WHERE owner_id = $1::uuid "
                            " AND id = $2::uuid",
                            owner,
                            id,
                            out.task["id"].get<std::string>());
                        out.created = true;
                    }
                    out.visit = *find_in(txn, owner, id);
                    return out;
                });
            },
            &detail::translate);
    }

private:
    static constexpr const char* kColumns =
        "v.id::text AS id, v.person_id::text AS person_id, p.name AS person_name, v.provider_id::text AS provider_id, "
        "d.name AS provider_name, to_char(v.date, 'YYYY-MM-DD') AS date, v.reason, v.outcome, "
        "to_char(v.follow_up, 'YYYY-MM-DD') AS follow_up, v.follow_up_task_id::text AS follow_up_task_id";
    static constexpr const char* kFrom =
        " FROM medical_visits v JOIN medical_people p ON p.id = v.person_id "
        " LEFT JOIN medical_providers d ON d.id = v.provider_id";

    template <typename Txn>
    static void check_refs(Txn& txn, const std::string& owner, const Input& in) {
        if (!detail::owns(txn, "medical_people", owner, in.person_id)) {
            throw UnknownReference("person_id");
        }
        if (!detail::owns(txn, "medical_providers", owner, in.provider_id)) {
            throw UnknownReference("provider_id");
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        return detail::one(
            txn,
            std::string("SELECT ") + kColumns + kFrom + " WHERE v.owner_id = $1::uuid AND v.id = $2::uuid",
            owner,
            id);
    }
};

// ── records ─────────────────────────────────────────────────────────────────

class RecordRepository {
public:
    struct Input {
        std::string person_id;
        std::optional<std::string> visit_id;
        std::string date;
        std::string kind;
        std::string title;
        std::string note;
    };

    /// Newest first; @p person and @p kind "" mean any.
    nlohmann::json list(const std::string& owner, const std::string& person, const std::string& kind) {
        return Database::get().execute_read([&](auto& txn) {
            return detail::many(txn,
                                std::string("SELECT ") + kColumns + kFrom +
                                    " WHERE r.owner_id = $1::uuid AND ($2 = '' OR r.person_id::text = $2) "
                                    " AND ($3 = '' OR r.kind = $3) ORDER BY r.date DESC, r.created_at DESC, r.id",
                                owner,
                                person,
                                kind);
        });
    }

    std::optional<nlohmann::json> get(const std::string& owner, const std::string& id) {
        return Database::get().execute_read([&](auto& txn) { return find_in(txn, owner, id); });
    }

    /// @throws UnknownReference.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_refs(txn, owner, in);
                    auto r = txn.exec_params(
                        "INSERT INTO medical_records (owner_id, person_id, visit_id, date, kind, title, note) "
                        "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::date, $5, $6, $7) RETURNING id::text",
                        owner,
                        in.person_id,
                        in.visit_id,
                        in.date,
                        in.kind,
                        in.title,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @throws NotFound, UnknownReference.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_refs(txn, owner, in);
                    auto r = txn.exec_params(
                        "UPDATE medical_records SET person_id = $3::uuid, visit_id = $4::uuid, date = $5::date, "
                        " kind = $6, title = $7, note = $8 WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.person_id,
                        in.visit_id,
                        in.date,
                        in.kind,
                        in.title,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("record");
                    }
                    return *find_in(txn, owner, id);
                });
            },
            &detail::translate);
    }

    /// Its files go with it (a trigger of migration 051 hands their objects to the sweep). @throws NotFound.
    void remove(const std::string& owner, const std::string& id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM medical_records WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("record");
            }
            return true;
        });
    }

private:
    static constexpr const char* kColumns =
        "r.id::text AS id, r.person_id::text AS person_id, p.name AS person_name, r.visit_id::text AS visit_id, "
        "to_char(r.date, 'YYYY-MM-DD') AS date, r.kind, r.title, r.note, "
        "(SELECT count(*) FROM private_files f WHERE f.owner_id = r.owner_id AND f.parent_kind = 'medical_record' "
        " AND f.parent_id = r.id AND f.status = 'ready')::int AS files";
    static constexpr const char* kFrom = " FROM medical_records r JOIN medical_people p ON p.id = r.person_id";

    template <typename Txn>
    static void check_refs(Txn& txn, const std::string& owner, const Input& in) {
        if (!detail::owns(txn, "medical_people", owner, in.person_id)) {
            throw UnknownReference("person_id");
        }
        if (!detail::owns(txn, "medical_visits", owner, in.visit_id)) {
            throw UnknownReference("visit_id");
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        return detail::one(
            txn,
            std::string("SELECT ") + kColumns + kFrom + " WHERE r.owner_id = $1::uuid AND r.id = $2::uuid",
            owner,
            id);
    }
};

// ── medications ─────────────────────────────────────────────────────────────

class MedicationRepository {
public:
    struct Input {
        std::string person_id;
        std::string name;
        std::string dose;
        std::string schedule;
        std::optional<std::string> start_date;
        std::optional<std::string> end_date;
        std::string note;
    };

    /// The current ones (no end, or an end on or after @p today) first by name, then the
    /// finished ones, the latest end first. @p person "" means everyone; @p current_only
    /// leaves the finished ones out.
    nlohmann::json list(const std::string& owner,
                        const std::string& person,
                        const std::string& today,
                        bool current_only) {
        return Database::get().execute_read([&](auto& txn) {
            return detail::many(txn,
                                std::string("SELECT ") + kColumns + kFrom +
                                    " WHERE m.owner_id = $1::uuid AND ($2 = '' OR m.person_id::text = $2) "
                                    " AND (NOT $4::boolean OR m.end_date IS NULL OR m.end_date >= $3::date) "
                                    " ORDER BY (m.end_date IS NOT NULL AND m.end_date < $3::date), "
                                    " CASE WHEN m.end_date < $3::date THEN m.end_date END DESC, m.name, m.id",
                                owner,
                                person,
                                today,
                                current_only);
        });
    }

    /// @throws UnknownReference.
    nlohmann::json create(const std::string& owner, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_refs(txn, owner, in);
                    auto r = txn.exec_params(
                        "INSERT INTO medical_medications (owner_id, person_id, name, dose, schedule, start_date, "
                        " end_date, note) VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6::date, $7::date, $8) "
                        "RETURNING id::text",
                        owner,
                        in.person_id,
                        in.name,
                        in.dose,
                        in.schedule,
                        in.start_date,
                        in.end_date,
                        in.note);
                    return *find_in(txn, owner, r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    /// @throws NotFound, UnknownReference.
    nlohmann::json replace(const std::string& owner, const std::string& id, const Input& in) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    check_refs(txn, owner, in);
                    auto r = txn.exec_params(
                        "UPDATE medical_medications SET person_id = $3::uuid, name = $4, dose = $5, schedule = $6, "
                        " start_date = $7::date, end_date = $8::date, note = $9 "
                        "WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id",
                        owner,
                        id,
                        in.person_id,
                        in.name,
                        in.dose,
                        in.schedule,
                        in.start_date,
                        in.end_date,
                        in.note);
                    if (r.empty()) {
                        throw NotFound("medication");
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
                "DELETE FROM medical_medications WHERE owner_id = $1::uuid AND id = $2::uuid RETURNING id", owner, id);
            if (r.empty()) {
                throw NotFound("medication");
            }
            return true;
        });
    }

    struct TaskAnswer {
        nlohmann::json medication;
        nlohmann::json task;
        bool created = false;
    };

    /// The task of the end date (a refill or a check before the course runs out): the linked
    /// one while it exists, else a new open task in area health due that day. @throws NotFound, NoDate.
    TaskAnswer end_task(const std::string& owner, const std::string& id) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    std::optional<nlohmann::json> row =
                        detail::one(txn,
                                    "SELECT m.id::text AS id, m.name, to_char(m.end_date, 'YYYY-MM-DD') AS end_date, "
                                    " m.end_task_id::text AS task_id, p.name AS person "
                                    " FROM medical_medications m JOIN medical_people p ON p.id = m.person_id "
                                    " WHERE m.owner_id = $1::uuid AND m.id = $2::uuid FOR UPDATE OF m",
                                    owner,
                                    id);
                    if (!row) {
                        throw NotFound("medication");
                    }
                    if (!(*row)["end_date"].is_string()) {
                        throw NoDate("the medication");
                    }
                    TaskAnswer out;
                    if (auto task = detail::existing_task(txn, owner, (*row)["task_id"])) {
                        out.task = *task;
                    } else {
                        out.task = detail::new_task(
                            txn,
                            owner,
                            (*row)["person"].get<std::string>() + ": " + (*row)["name"].get<std::string>() + " ends",
                            (*row)["end_date"].get<std::string>(),
                            "The course ends this day: a refill, a check-up, or nothing.");
                        txn.exec_params(
                            "UPDATE medical_medications SET end_task_id = $3::uuid WHERE owner_id = $1::uuid "
                            " AND id = $2::uuid",
                            owner,
                            id,
                            out.task["id"].get<std::string>());
                        out.created = true;
                    }
                    out.medication = *find_in(txn, owner, id);
                    return out;
                });
            },
            &detail::translate);
    }

private:
    static constexpr const char* kColumns =
        "m.id::text AS id, m.person_id::text AS person_id, p.name AS person_name, m.name, m.dose, m.schedule, "
        "to_char(m.start_date, 'YYYY-MM-DD') AS start_date, to_char(m.end_date, 'YYYY-MM-DD') AS end_date, "
        "m.end_task_id::text AS end_task_id, m.note";
    static constexpr const char* kFrom = " FROM medical_medications m JOIN medical_people p ON p.id = m.person_id";

    template <typename Txn>
    static void check_refs(Txn& txn, const std::string& owner, const Input& in) {
        if (!detail::owns(txn, "medical_people", owner, in.person_id)) {
            throw UnknownReference("person_id");
        }
    }

    template <typename Txn>
    static std::optional<nlohmann::json> find_in(Txn& txn, const std::string& owner, const std::string& id) {
        return detail::one(
            txn,
            std::string("SELECT ") + kColumns + kFrom + " WHERE m.owner_id = $1::uuid AND m.id = $2::uuid",
            owner,
            id);
    }
};

}  // namespace Repositories::Medical
