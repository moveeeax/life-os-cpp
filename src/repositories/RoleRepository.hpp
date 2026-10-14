/**
 * @file RoleRepository.hpp
 * @brief All SQL touching `roles` lives here.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <vector>

#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "repositories/CrudBase.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories {

// Stable 409 codes / 404 resource carried on the exception, so with_repo_errors()
// maps them without including this header.
struct DuplicateRole : ConflictError {
    DuplicateRole() : ConflictError("role_exists", "A role with that name already exists") {}
};

struct RoleNotFound : NotFoundError {
    RoleNotFound() : NotFoundError("role") {}
};

struct RoleInUse : ConflictError {
    RoleInUse() : ConflictError("role_in_use", "Reassign users away from this role before deleting") {}
};

/// Every sign-up lands in the default role, so it must never carry the admin bit.
struct DefaultRoleAdmin : ValidationError {
    DefaultRoleAdmin()
        : ValidationError("default_role_admin", "The default role cannot hold the administer permission") {}
};

/// Taking the admin bit from the only role that has it would leave nobody
/// able to administer the instance.
struct LastAdminRole : ConflictError {
    LastAdminRole() : ConflictError("last_admin_role", "At least one role must keep the administer permission") {}
};

class RoleRepository : public CrudBase<RoleRepository, Domain::Role, int> {
public:
    // CrudBase contract — supplies find(id) / list(limit,offset) / count().
    static constexpr const char* kTable = "roles";
    static constexpr const char* kColumns = "id, name, permissions, is_default, created_at";
    static constexpr const char* kIdColumn = "id";
    static constexpr const char* kOrderBy = "id";

    std::optional<Domain::Role> find_by_name(const std::string& name) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Domain::Role> {
            auto r = txn.exec_params("SELECT id, name, permissions, is_default, created_at FROM roles WHERE name = $1",
                                     name);
            if (r.empty())
                return std::nullopt;
            return Domain::Role::from_row(r[0]);
        });
    }

    /**
     * @brief Default role for new sign-ups. Migration 001 marks "User" as
     *        the default; this query is idempotent if the seed didn't run.
     */
    std::optional<Domain::Role> find_default() {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Domain::Role> {
            auto r = txn.exec(
                "SELECT id, name, permissions, is_default, created_at "
                "FROM roles WHERE is_default = TRUE LIMIT 1");
            if (r.empty())
                return std::nullopt;
            return Domain::Role::from_row(r[0]);
        });
    }

    /**
     * @brief Insert a new role. Throws DuplicateRole on UNIQUE(name),
     *        DefaultRoleAdmin when a default role would hold the admin bit.
     *        is_default=true triggers a transaction-wide flip: only one
     *        row may be the default at a time, so we clear any prior
     *        default first. Migration 001 enforces the same invariant
     *        with a partial UNIQUE index.
     */
    Domain::Role create(const std::string& name, std::uint32_t permissions, bool is_default) {
        if (is_default && is_admin_mask(permissions))
            throw DefaultRoleAdmin{};
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    if (is_default)
                        txn.exec("UPDATE roles SET is_default = FALSE WHERE is_default = TRUE");
                    auto r = txn.exec_params(
                        "INSERT INTO roles (name, permissions, is_default) VALUES ($1, $2, $3) "
                        "RETURNING id, name, permissions, is_default, created_at",
                        name,
                        static_cast<int>(permissions),
                        is_default);
                    return Domain::Role::from_row(r[0]);
                });
            },
            detail::throw_on<DuplicateRole>("23505"));
    }

    /**
     * @brief Partial update: pass nullopt for any field that should keep
     *        its current value. Throws RoleNotFound on missing id,
     *        DuplicateRole on UNIQUE(name) collision, DefaultRoleAdmin when
     *        the result would be a default role with the admin bit, and
     *        LastAdminRole when it would take the admin bit from the only
     *        role holding it.
     *
     *        The target row and every admin-bit row are locked (FOR UPDATE)
     *        before the checks, so two concurrent demotions of the last two
     *        admin roles serialize and the second one sees the first.
     */
    Domain::Role update(int id,
                        std::optional<std::string> name,
                        std::optional<std::uint32_t> permissions,
                        std::optional<bool> is_default) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    const int admin_bit = static_cast<int>(Domain::Permission::kAdminister);
                    auto locked = txn.exec_params(
                        "SELECT id, permissions, is_default FROM roles "
                        "WHERE id = $1 OR (permissions & $2) <> 0 ORDER BY id FOR UPDATE",
                        id,
                        admin_bit);
                    std::optional<std::uint32_t> current_perms;
                    bool current_default = false;
                    int other_admin_roles = 0;
                    for (const auto& row : locked) {
                        const auto row_perms = static_cast<std::uint32_t>(row["permissions"].template as<int>());
                        if (row["id"].template as<int>() == id) {
                            current_perms = row_perms;
                            current_default = row["is_default"].template as<bool>();
                        } else if (is_admin_mask(row_perms)) {
                            ++other_admin_roles;
                        }
                    }
                    if (!current_perms)
                        throw RoleNotFound{};
                    const std::uint32_t next_perms = permissions.value_or(*current_perms);
                    const bool next_default = is_default.value_or(current_default);
                    if (next_default && is_admin_mask(next_perms))
                        throw DefaultRoleAdmin{};
                    if (is_admin_mask(*current_perms) && !is_admin_mask(next_perms) && other_admin_roles == 0)
                        throw LastAdminRole{};
                    if (is_default && *is_default) {
                        // Clear any existing default before flipping this row on.
                        txn.exec_params("UPDATE roles SET is_default = FALSE WHERE id <> $1 AND is_default = TRUE", id);
                    }
                    auto r = txn.exec_params(
                        "UPDATE roles SET "
                        "  name        = COALESCE($1, name), "
                        "  permissions = COALESCE($2, permissions), "
                        "  is_default  = COALESCE($3, is_default) "
                        "WHERE id = $4 "
                        "RETURNING id, name, permissions, is_default, created_at",
                        name,
                        permissions ? std::optional<int>(static_cast<int>(*permissions)) : std::nullopt,
                        is_default,
                        id);
                    if (r.empty())
                        throw RoleNotFound{};
                    return Domain::Role::from_row(r[0]);
                });
            },
            detail::throw_on<DuplicateRole>("23505"));
    }

    /**
     * @brief Delete a role. Throws RoleNotFound if it doesn't exist,
     *        RoleInUse on FK violation (SQLSTATE 23503) — the migration's
     *        ON DELETE RESTRICT keeps the schema consistent.
     */
    void remove(int id) {
        detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params("DELETE FROM roles WHERE id = $1 RETURNING id", id);
                    if (r.empty())
                        throw RoleNotFound{};
                    return 0;
                });
            },
            detail::throw_on<RoleInUse>("23503"));
    }

private:
    static bool is_admin_mask(std::uint32_t permissions) {
        return (permissions & Domain::Permission::kAdminister) == Domain::Permission::kAdminister;
    }
};

}  // namespace Repositories
