/**
 * @file UserRepository.hpp
 * @brief All SQL touching `users` lives here.
 *
 * Everything is funnelled through one class so controllers never touch
 * pqxx directly.
 *
 * Constraint violations surface as typed exceptions (DuplicateEmail,
 * UserNotFound) so the HTTP layer can map them to 409/404 without
 * sniffing pqxx::sql_error::sqlstate() every time.
 */

#pragma once

#include <optional>
#include <pqxx/pqxx>
#include <string>
#include <string_view>
#include <vector>

#include "database/Database.hpp"
#include "domain/Role.hpp"
#include "domain/User.hpp"
#include "repositories/FitnessData.hpp"
#include "repositories/RepoErrors.hpp"
#include "repositories/SqlErrors.hpp"

namespace Repositories {

// The 409 code / 404 resource the HTTP layer reports live ON the exception now,
// so the shared with_repo_errors() helper maps them without knowing this type.
struct DuplicateEmail : ConflictError {
    DuplicateEmail() : ConflictError("email_taken", "Email is already registered") {}
};

struct UserNotFound : NotFoundError {
    UserNotFound() : NotFoundError("user") {}
};

/**
 * @brief → 409. Migration 007 added ON DELETE RESTRICT foreign keys from
 *        `payments`, `wallet_entries` and `wallet_balances` to `users` — a
 *        user with any billing history can no longer be hard-deleted.
 *        Mirrors RoleRepository::RoleInUse exactly.
 */
struct UserHasBillingHistory : ConflictError {
    UserHasBillingHistory()
        : ConflictError("user_has_billing_history", "This user has payment or wallet history and cannot be deleted") {}
};

class UserRepository {
public:
    /**
     * @brief Columns selected by every query that returns a User joined
     *        with its Role. Kept once so all loaders agree on the shape.
     */
    static constexpr const char* kSelectWithRole =
        "SELECT u.id, u.email, u.password_hash, u.first_name, u.last_name, "
        "       u.confirmed, u.role_id, u.created_at, u.updated_at, "
        "       r.id  AS r_id, r.name AS r_name, r.permissions AS r_permissions, "
        "       r.is_default AS r_is_default, r.created_at AS r_created_at "
        "FROM users u JOIN roles r ON r.id = u.role_id ";

    template <typename Row>
    static Domain::User user_with_role_from_row(const Row& row) {
        Domain::User u = Domain::User::from_row(row);
        Domain::Role r;
        r.id = row["r_id"].template as<int>();
        r.name = row["r_name"].template as<std::string>();
        r.permissions = static_cast<std::uint32_t>(row["r_permissions"].template as<int>());
        r.is_default = row["r_is_default"].template as<bool>();
        r.created_at = row["r_created_at"].template as<std::string>();
        u.role = std::move(r);
        return u;
    }

    /**
     * @param from_primary Read from the primary instead of a replica. Set when
     *        re-reading a row the same request/job just wrote (read-after-write)
     *        so replica lag can't return a stale row or a spurious "not found".
     */
    std::optional<Domain::User> find(const std::string& id, bool from_primary = false) {
        return find_by_("u.id", id, from_primary);
    }

    std::optional<Domain::User> find_by_email(const std::string& email) { return find_by_("u.email", email); }

private:
    /// Shared single-row loader behind find / find_by_email: same SELECT,
    /// only the WHERE column differs. @p col is a trusted compile-time
    /// column name, never user input.
    std::optional<Domain::User> find_by_(const char* col, const std::string& value, bool from_primary = false) {
        auto query = [&](auto& txn) -> std::optional<Domain::User> {
            auto r = txn.exec_params(std::string(kSelectWithRole) + "WHERE " + col + " = $1", value);
            if (r.empty())
                return std::nullopt;
            return user_with_role_from_row(r[0]);
        };
        return from_primary ? Database::get().execute_read_primary(query) : Database::get().execute_read(query);
    }

public:
    std::vector<Domain::User> list(int limit = 100, int offset = 0) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                std::string(kSelectWithRole) + "ORDER BY u.created_at DESC LIMIT $1 OFFSET $2", limit, offset);
            std::vector<Domain::User> out;
            out.reserve(r.size());
            for (const auto& row : r)
                out.push_back(user_with_role_from_row(row));
            return out;
        });
    }

    /**
     * @brief INSERT a new user. Throws DuplicateEmail on UNIQUE violation
     *        (SQLSTATE 23505) so the caller can return 409.
     *
     * @param password_hash Optional. Pass nullopt for invite-only flow
     *                      where the user sets their password later.
     */
    Domain::User create(const std::string& email,
                        const std::optional<std::string>& password_hash,
                        const std::optional<std::string>& first_name,
                        const std::optional<std::string>& last_name,
                        int role_id,
                        bool confirmed = false) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "INSERT INTO users (email, password_hash, first_name, last_name, role_id, confirmed) "
                        "VALUES ($1, $2, $3, $4, $5, $6) "
                        "RETURNING id, email, password_hash, first_name, last_name, confirmed, "
                        "          role_id, created_at, updated_at",
                        email,
                        password_hash,
                        first_name,
                        last_name,
                        role_id,
                        confirmed);
                    // The role isn't joined here — callers that need it
                    // attach the Role they already loaded (no extra query).
                    return Domain::User::from_row(r[0]);
                });
            },
            detail::throw_on<DuplicateEmail>("23505"));
    }

    /**
     * @brief Set a new password hash only while the stored one still equals
     *        @p expected_hash (nullopt = no password yet). False when the
     *        hash moved on (another reset link or a password change won the
     *        race) or the user is gone.
     */
    bool replace_password_hash(const std::string& id,
                               const std::optional<std::string>& expected_hash,
                               const std::string& new_hash) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE users SET password_hash = $1 "
                "WHERE id = $2 AND password_hash IS NOT DISTINCT FROM $3 RETURNING id",
                new_hash,
                id,
                expected_hash);
            return !r.empty();
        });
    }

    void update_password_hash(const std::string& id, const std::string& new_hash) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("UPDATE users SET password_hash = $1 WHERE id = $2 RETURNING id", new_hash, id);
            if (r.empty())
                throw UserNotFound{};
            return 0;
        });
    }

    /**
     * @brief Mark the user confirmed. Idempotent — re-confirming a
     *        confirmed user is a no-op, returns true.
     * @note Deliberately returns bool (false = no such user) instead of
     *       throwing UserNotFound like the other mutators: confirmation is a
     *       fire-once flow where "no row" is an expected branch the caller
     *       maps to 404, not an exceptional repo error.
     */
    bool mark_confirmed(const std::string& id) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("UPDATE users SET confirmed = TRUE WHERE id = $1 RETURNING id", id);
            return !r.empty();
        });
    }

    /**
     * @brief Redeem an invite: set the initial password and confirm the account
     *        in a single write. The WHERE clause makes this a true one-shot at
     *        the DB layer — it only matches a still-pending invite (no password
     *        yet, unconfirmed), so a replayed/captured invite link cannot reset
     *        an already-active account's password (the Redis replay guard is
     *        fail-open and must not be the only defense).
     * @return true if the invite was redeemed; false if no pending invite
     *         matched (already redeemed, already confirmed, or gone).
     */
    bool redeem_invite(const std::string& id, const std::string& password_hash) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE users SET password_hash = $1, confirmed = TRUE "
                "WHERE id = $2 AND confirmed = FALSE AND password_hash IS NULL RETURNING id",
                password_hash,
                id);
            return !r.empty();
        });
    }

    /**
     * @brief Change email. Throws DuplicateEmail on unique-violation,
     *        UserNotFound if the user is gone.
     */
    void change_email(const std::string& id, const std::string& new_email) {
        detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params("UPDATE users SET email = $1 WHERE id = $2 RETURNING id", new_email, id);
                    if (r.empty())
                        throw UserNotFound{};
                    return 0;
                });
            },
            detail::throw_on<DuplicateEmail>("23505"));
    }

    /**
     * @brief Admin partial update — email / role / names in ONE statement
     *        (and therefore one transaction), so a constraint failure can't
     *        leave a half-applied patch the way three sequential mutations
     *        could. Pass nullopt to keep a field's current value.
     *        Throws DuplicateEmail (23505) and UserNotFound like the
     *        single-field mutators.
     */
    void admin_update(const std::string& id,
                      const std::optional<std::string>& email,
                      std::optional<int> role_id,
                      const std::optional<std::string>& first_name,
                      const std::optional<std::string>& last_name) {
        detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE users SET "
                        "  email      = COALESCE($1, email), "
                        "  role_id    = COALESCE($2, role_id), "
                        "  first_name = COALESCE($3, first_name), "
                        "  last_name  = COALESCE($4, last_name) "
                        "WHERE id = $5 RETURNING id",
                        email,
                        role_id,
                        first_name,
                        last_name,
                        id);
                    if (r.empty())
                        throw UserNotFound{};
                    return 0;
                });
            },
            detail::throw_on<DuplicateEmail>("23505"));
    }

    void change_role(const std::string& id, int new_role_id) {
        Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params("UPDATE users SET role_id = $1 WHERE id = $2 RETURNING id", new_role_id, id);
            if (r.empty())
                throw UserNotFound{};
            return 0;
        });
    }

    /**
     * @brief The per-user tables of the modules, in an order their foreign
     *        keys accept. Their owner_id carries no foreign key to users
     *        (migration 017), so a user delete removes these rows itself.
     *        Child tables with ON DELETE CASCADE (session_exercises,
     *        workout_sets, routine_exercises) go with their parents;
     *        mi_accounts cascades from users.
     */
    static constexpr const char* kOwnedTables[] = {
        "workout_sessions",
        "routines",
        "exercises",
        "money_transactions",
        "money_transfers",
        "money_merchants",
        "money_categories",
        "money_accounts",
        "money_currencies",
        "money_settings",
        "money_parse_jobs",
        "money_advisor_reports",
        "task_parse_jobs",
        "task_notes",
        "task_items",
        "goal_checkins",
        "goal_milestones",
        "goal_sections",
        "goal_items",
        "food_entries",
        "food_items",
        "food_goals",
        "food_parse_jobs",
        "day_focus",
        "travel_trip_items",
        "travel_trip_contacts",
        "travel_trip_moments",
        "travel_trips",
        "travel_rules",
        "travel_crossings",
        "travel_settings",
        "travel_documents",
        "now_settings",
        "day_note",
        "mood_checkins",
        "tape_measurements",
        "habit_checks",
        "habits",
        "notes",
        "projects",
        "practice_entries",
        "practice_deals",
        "practice_clients",
        // A deleted private_files row leaves its object key in private_file_purges
        // (a trigger of migration 051); the files_sweep job removes the objects.
        "private_files",
        "medical_medications",
        "medical_records",
        "medical_visits",
        "medical_providers",
        "medical_people",
        // A deleted ani_agents row queues its snapshots in private_file_purges
        // (a trigger of migration 058); its pod goes with the next reconcile.
        "ani_agents",
        "ani_usage",
        "ani_actions",
        "ani_turns",
        "ani_inbox",
        "ani_hooks",
        "ani_prefs",
        "ani_reminders",
        "ani_facts",
        "leetcode_snapshots",
        "leetcode_profiles",
        "telegram_cursors",
        "telegram_link",
        "telegram_waiting",
        "telegram_quiet",
        "calendar_links",
        "calendar_states",
        "focus_sessions",
    };

    /// The foreign keys of the billing tables (migration 007, ON DELETE
    /// RESTRICT) that keep a user with payment or wallet history.
    static constexpr const char* kBillingConstraints[] = {
        "payments_user_id_fkey",
        "wallet_entries_user_id_fkey",
        "wallet_balances_user_id_fkey",
    };

    /// Whether a foreign-key violation names one of kBillingConstraints
    /// (PostgreSQL quotes the constraint name in the message).
    static bool is_billing_violation(const std::string& message) {
        for (const char* name : kBillingConstraints) {
            if (message.find(std::string("\"") + name + "\"") != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Delete a user and every module row they own, in one transaction:
     *        the fitness rows of the Xiaomi account the user linked (keyed by
     *        the Xiaomi id, not by owner_id), the owned module tables, and the
     *        user row (mi_accounts cascades from it).
     *        Throws UserNotFound if it doesn't exist, UserHasBillingHistory
     *        when a payment or wallet_entries/wallet_balances row still
     *        references this user (ON DELETE RESTRICT, migration 007). Any
     *        other foreign-key violation is rethrown unchanged: it means a
     *        table is missing from kOwnedTables, not billing history.
     */
    void remove(const std::string& id) {
        try {
            Database::get().execute_write([&](auto& txn) {
                auto linked = txn.exec_params("SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid", id);
                if (!linked.empty()) {
                    FitnessData::delete_in(txn, linked[0][0].template as<std::string>());
                }
                for (const char* table : kOwnedTables) {
                    txn.exec_params(std::string("DELETE FROM ") + table + " WHERE owner_id = $1::uuid", id);
                }
                auto r = txn.exec_params("DELETE FROM users WHERE id = $1 RETURNING id", id);
                if (r.empty())
                    throw UserNotFound{};
                return 0;
            });
        } catch (const pqxx::sql_error& e) {
            if (Repositories::detail::is_fk_violation(e.sqlstate()) && is_billing_violation(e.what()))
                throw UserHasBillingHistory{};
            throw;
        }
    }

    /**
     * @brief Total count for pagination headers.
     */
    long count() {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec("SELECT COUNT(*) FROM users");
            return r[0][0].template as<long>();
        });
    }
};

}  // namespace Repositories
