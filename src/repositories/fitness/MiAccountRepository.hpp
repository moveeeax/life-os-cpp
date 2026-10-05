/**
 * @file MiAccountRepository.hpp
 * @brief The Xiaomi account a user linked (migration 020, table mi_accounts).
 *
 * One row per Life OS user and one user per Xiaomi account. The passToken is
 * sealed with the key from the environment (it arrives via the constructor
 * and never reaches the database); Xiaomi may rotate the token on a login,
 * and the rotation updates the row in place.
 *
 * A user's fitness rows are the rows whose user_id equals the linked
 * xiaomi_user_id: the data tables carry the Xiaomi id, this table maps it to
 * the user.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "fitness/xiaomi/Credentials.hpp"
#include "fitness/xiaomi/Crypto.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "repositories/RepoErrors.hpp"

namespace Repositories {

struct AccountLinkedElsewhere : ConflictError {
    AccountLinkedElsewhere()
        : ConflictError("account_linked_elsewhere", "this Xiaomi account is linked to another user") {}
};

struct DifferentAccount : ConflictError {
    DifferentAccount()
        : ConflictError("different_account", "another Xiaomi account is linked; unlink it before linking this one") {}
};

class MiAccountRepository {
public:
    explicit MiAccountRepository(std::string key_b64) : key_b64_(std::move(key_b64)) {}

    /// Credentials of this user's account, or nullopt without a link.
    std::optional<Xiaomi::Credentials> load(const std::string& owner_id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Xiaomi::Credentials> {
            auto r = txn.exec_params(
                "SELECT xiaomi_user_id, pass_token_sealed, nonce, region FROM mi_accounts WHERE owner_id = $1::uuid",
                owner_id);
            if (r.empty()) {
                return std::nullopt;
            }
            return unseal_row(r[0]);
        });
    }

    /// The oldest link with its owner. The sync job uses it while the sync is
    /// still one per system.
    std::optional<std::pair<std::string, Xiaomi::Credentials>> load_first() {
        return Database::get().execute_read(
            [&](auto& txn) -> std::optional<std::pair<std::string, Xiaomi::Credentials>> {
                auto r = txn.exec("SELECT owner_id::text AS owner_id, xiaomi_user_id, pass_token_sealed, nonce, region "
                                  "FROM mi_accounts ORDER BY linked_at, owner_id LIMIT 1");
                if (r.empty()) {
                    return std::nullopt;
                }
                return std::make_pair(r[0]["owner_id"].template as<std::string>(), unseal_row(r[0]));
            });
    }

    /// Region of the oldest link, without touching the token. For computing
    /// the day bounds of a scheduled sync.
    std::optional<std::string> first_region() {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec("SELECT region FROM mi_accounts ORDER BY linked_at, owner_id LIMIT 1");
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    /// Region of this user's link, or nullopt.
    std::optional<std::string> region_of(const std::string& owner_id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<std::string> {
            auto r = txn.exec_params("SELECT region FROM mi_accounts WHERE owner_id = $1::uuid", owner_id);
            if (r.empty()) {
                return std::nullopt;
            }
            return r[0][0].template as<std::string>();
        });
    }

    /**
     * @brief Create the user's link, or replace the token of the existing one
     *        (linking again after reauth_required). The link becomes `ok`.
     * @throws AccountLinkedElsewhere when another user holds this Xiaomi account.
     * @throws DifferentAccount when the user has another Xiaomi account linked.
     */
    void link(const std::string& owner_id, const Xiaomi::Credentials& credentials, bool region_detected) {
        validate_identity(credentials);
        Xiaomi::validate_pass_token(credentials.pass_token);
        const Xiaomi::Sealed sealed = Xiaomi::seal(credentials.pass_token, key_b64_);
        const std::string ciphertext_b64 = Xiaomi::Crypto::b64_encode(sealed.ciphertext);
        const std::string nonce_b64 = Xiaomi::Crypto::b64_encode(sealed.nonce);

        Database::get().execute_write([&](auto& txn) {
            // Both checks and the write share the transaction; the UNIQUE
            // constraints catch what a concurrent link slips past them.
            auto taken = txn.exec_params(
                "SELECT 1 FROM mi_accounts WHERE xiaomi_user_id = $1 AND owner_id <> $2::uuid FOR UPDATE",
                credentials.user_id,
                owner_id);
            if (!taken.empty()) {
                throw AccountLinkedElsewhere();
            }
            auto own = txn.exec_params(
                "SELECT xiaomi_user_id FROM mi_accounts WHERE owner_id = $1::uuid FOR UPDATE", owner_id);
            if (!own.empty() && own[0][0].template as<std::string>() != credentials.user_id) {
                throw DifferentAccount();
            }
            txn.exec_params(
                "INSERT INTO mi_accounts (owner_id, xiaomi_user_id, pass_token_sealed, nonce, region, "
                " region_detected, status, last_ok_at, last_error) "
                "VALUES ($1::uuid, $2, $3, $4, $5, $6, 'ok', now(), NULL) "
                "ON CONFLICT (owner_id) DO UPDATE SET "
                " pass_token_sealed = EXCLUDED.pass_token_sealed, nonce = EXCLUDED.nonce, "
                " region = EXCLUDED.region, region_detected = EXCLUDED.region_detected, "
                " status = 'ok', last_ok_at = now(), last_error = NULL, rotated_at = now()",
                owner_id,
                credentials.user_id,
                ciphertext_b64,
                nonce_b64,
                credentials.region,
                region_detected);
            return true;
        });
    }

    /// Write the token Xiaomi rotated during a login. A link that was removed
    /// in the meantime stays removed.
    void store_rotated(const std::string& owner_id, const Xiaomi::Credentials& credentials) {
        validate_identity(credentials);
        Xiaomi::validate_pass_token(credentials.pass_token);
        const Xiaomi::Sealed sealed = Xiaomi::seal(credentials.pass_token, key_b64_);
        const std::string ciphertext_b64 = Xiaomi::Crypto::b64_encode(sealed.ciphertext);
        const std::string nonce_b64 = Xiaomi::Crypto::b64_encode(sealed.nonce);

        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE mi_accounts SET pass_token_sealed = $3, nonce = $4, rotated_at = now() "
                "WHERE owner_id = $1::uuid AND xiaomi_user_id = $2",
                owner_id,
                credentials.user_id,
                ciphertext_b64,
                nonce_b64);
            return true;
        });
    }

    /// @returns false without a link.
    bool set_region(const std::string& owner_id, const std::string& region, bool detected) {
        if (!Xiaomi::is_known_region(region) || region.empty()) {
            throw Xiaomi::MiFitnessAuthError("region is not one of the known candidates");
        }
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "UPDATE mi_accounts SET region = $2, region_detected = $3 WHERE owner_id = $1::uuid RETURNING 1",
                owner_id,
                region,
                detected);
            return !r.empty();
        });
    }

    /// Xiaomi accepted the stored token.
    void mark_ok(const std::string& owner_id) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE mi_accounts SET status = 'ok', last_ok_at = now(), last_error = NULL "
                "WHERE owner_id = $1::uuid",
                owner_id);
            return true;
        });
    }

    /// Xiaomi refused the stored token: the user has to link again.
    void mark_reauth_required(const std::string& owner_id, const std::string& error_code) {
        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "UPDATE mi_accounts SET status = 'reauth_required', last_error = $2 WHERE owner_id = $1::uuid",
                owner_id,
                error_code);
            return true;
        });
    }

    /**
     * @brief The link as the profile shows it, or nullopt without a link:
     *        {status, xiaomi_user_id, region, region_detected, linked_at,
     *        last_ok_at, last_error, last_sync}. The caller masks the id.
     */
    std::optional<nlohmann::json> status(const std::string& owner_id) {
        return Database::get().execute_read([&](auto& txn) -> std::optional<nlohmann::json> {
            auto r = txn.exec_params(
                "SELECT json_build_object("
                " 'status', a.status, 'xiaomi_user_id', a.xiaomi_user_id, 'region', a.region, "
                " 'region_detected', a.region_detected, "
                " 'linked_at', to_char(a.linked_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"'), "
                " 'last_ok_at', to_char(a.last_ok_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"'), "
                " 'last_error', a.last_error, "
                " 'last_sync', (SELECT json_build_object('id', s.id, 'status', s.status, 'finished_at', "
                "    to_char(s.finished_at AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS\"+00:00\"')) "
                "   FROM sync_runs s WHERE s.xiaomi_user_id = a.xiaomi_user_id AND s.finished_at IS NOT NULL "
                "   ORDER BY s.id DESC LIMIT 1)) "
                "FROM mi_accounts a WHERE a.owner_id = $1::uuid",
                owner_id);
            if (r.empty()) {
                return std::nullopt;
            }
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /**
     * @brief Remove the user's link. With @p delete_data the account's fitness
     *        rows, its sync runs and state go too, and the band data of the
     *        user's workout sessions is cleared. Without it the rows stay and
     *        show again when the same Xiaomi account is linked.
     * @returns false without a link.
     */
    bool unlink(const std::string& owner_id, bool delete_data) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "DELETE FROM mi_accounts WHERE owner_id = $1::uuid RETURNING xiaomi_user_id", owner_id);
            if (r.empty()) {
                return false;
            }
            if (!delete_data) {
                return true;
            }
            const std::string xiaomi_id = r[0][0].template as<std::string>();
            for (const char* table : {"daily_activity",
                                      "sleep_sessions",
                                      "workouts",
                                      "body_measurements",
                                      "heart_rate_samples",
                                      "spo2_samples",
                                      "stress_samples",
                                      "abnormal_heart_beat_events"}) {
                txn.exec_params(std::string("DELETE FROM ") + table + " WHERE user_id = $1", xiaomi_id);
            }
            txn.exec_params("DELETE FROM sync_runs WHERE xiaomi_user_id = $1", xiaomi_id);
            txn.exec_params("DELETE FROM sync_state WHERE xiaomi_user_id = $1", xiaomi_id);
            // The band data of finished workouts came from the deleted rows.
            txn.exec_params(
                "UPDATE workout_sessions SET health_status = 'pending', hr_avg = NULL, hr_max = NULL, "
                " hr_samples = 0, band_workout_id = NULL, band_calories_kcal = NULL, reconciled_at = NULL "
                "WHERE owner_id = $1::uuid AND finished_at IS NOT NULL",
                owner_id);
            return true;
        });
    }

private:
    template <typename Row>
    Xiaomi::Credentials unseal_row(const Row& row) const {
        Xiaomi::Sealed sealed;
        sealed.ciphertext = Xiaomi::Crypto::b64_decode(row["pass_token_sealed"].template as<std::string>());
        sealed.nonce = Xiaomi::Crypto::b64_decode(row["nonce"].template as<std::string>());

        Xiaomi::Credentials out;
        out.user_id = row["xiaomi_user_id"].template as<std::string>();
        out.region = row["region"].template as<std::string>();
        out.pass_token = Xiaomi::unseal(sealed, key_b64_);
        validate_identity(out);
        // Validated on read as well: the value could have reached the database
        // bypassing link(), and then the failure must happen here, not on a
        // Xiaomi request with an obscure header encoding error.
        Xiaomi::validate_pass_token(out.pass_token);
        return out;
    }

    /// user_id goes into the Cookie header next to the token, region into the
    /// cloud host name. CRLF in the former is header injection, an arbitrary
    /// string in the latter would send the request cookies to a foreign domain.
    static void validate_identity(const Xiaomi::Credentials& credentials) {
        Xiaomi::validate_pass_token(credentials.user_id);
        if (!Xiaomi::is_known_region(credentials.region)) {
            throw Xiaomi::MiFitnessAuthError("region is not one of the known candidates");
        }
    }

    std::string key_b64_;
};

}  // namespace Repositories
