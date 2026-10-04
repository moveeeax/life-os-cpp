/**
 * @file CredentialsRepository.hpp
 * @brief The single Xiaomi credentials row (migration 011).
 *
 * The service serves one account, so the table holds exactly one row and
 * writes go through ON CONFLICT: rotation updates it instead of piling up a
 * history of dead tokens.
 *
 * The encryption key comes from the environment via the constructor and never reaches the database.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "database/Database.hpp"
#include "fitness/xiaomi/Credentials.hpp"
#include "fitness/xiaomi/Crypto.hpp"
#include "fitness/xiaomi/Regions.hpp"
#include "utils/Config.hpp"

namespace Repositories {

class CredentialsRepository {
public:
    explicit CredentialsRepository(std::string key_b64) : key_b64_(std::move(key_b64)) {}

    /// Returns empty when the row does not exist yet: not an error, but a
    /// sign that credentials must be seeded from the Secret.
    std::optional<Xiaomi::Credentials> load() {
        return Database::get().execute_read([&](auto& txn) -> std::optional<Xiaomi::Credentials> {
            auto r = txn.exec_params(
                "SELECT user_id, pass_token_sealed, nonce, region FROM xiaomi_credentials "
                "ORDER BY rotated_at DESC LIMIT 1");
            if (r.empty()) {
                return std::nullopt;
            }
            const auto& row = r[0];

            Xiaomi::Sealed sealed;
            sealed.ciphertext = Xiaomi::Crypto::b64_decode(row["pass_token_sealed"].template as<std::string>());
            sealed.nonce = Xiaomi::Crypto::b64_decode(row["nonce"].template as<std::string>());

            Xiaomi::Credentials out;
            out.user_id = row["user_id"].template as<std::string>();
            out.region = row["region"].template as<std::string>();
            out.pass_token = Xiaomi::unseal(sealed, key_b64_);
            validate_identity(out);
            // Validated on read as well: the value could have reached the
            // database bypassing store, and then the failure must happen here,
            // not on a Xiaomi request with an obscure header encoding error.
            Xiaomi::validate_pass_token(out.pass_token);
            return out;
        });
    }

    /// Writes or updates the single row. The token is validated before the
    /// write: garbage in the database costs more than a rejection at the entry.
    void store(const Xiaomi::Credentials& credentials) {
        validate_identity(credentials);
        Xiaomi::validate_pass_token(credentials.pass_token);
        const Xiaomi::Sealed sealed = Xiaomi::seal(credentials.pass_token, key_b64_);
        const std::string ciphertext_b64 = Xiaomi::Crypto::b64_encode(sealed.ciphertext);
        const std::string nonce_b64 = Xiaomi::Crypto::b64_encode(sealed.nonce);

        Database::get().execute_write([&](auto& txn) {
            txn.exec_params(
                "INSERT INTO xiaomi_credentials (user_id, pass_token_sealed, nonce, region) "
                "VALUES ($1, $2, $3, $4) "
                "ON CONFLICT (user_id) DO UPDATE SET "
                "pass_token_sealed = EXCLUDED.pass_token_sealed, "
                "nonce = EXCLUDED.nonce, "
                "region = EXCLUDED.region, "
                "rotated_at = now()",
                credentials.user_id,
                ciphertext_b64,
                nonce_b64,
                credentials.region);
            return true;
        });
    }

private:
    /// user_id goes into the Cookie header next to the token, region into the
    /// cloud host name. CRLF in the former is header injection, an arbitrary
    /// string in the latter would send the request cookies to a foreign domain (review 2 finding).
    static void validate_identity(const Xiaomi::Credentials& credentials) {
        Xiaomi::validate_pass_token(credentials.user_id);
        if (!Xiaomi::is_known_region(credentials.region)) {
            throw Xiaomi::MiFitnessAuthError("region is not one of the known candidates");
        }
    }

    std::string key_b64_;
};

/**
 * @brief Seed Xiaomi credentials from config at service start.
 *
 * The cluster Secret is only a seed. A rotated token in the database wins:
 * Xiaomi issued it after the Secret was created, and overwriting it would roll
 * the session back to a dead value. The emergency lever xiaomi.reseed /
 * MI_FITNESS_RESEED overwrites the row deliberately: the token in the database
 * died, the owner supplied a fresh one.
 *
 * Returns true when the row was written. An incomplete seed is not an error
 * but "not configured": the service starts and answers not_configured.
 * An unreadable row with reseed disabled fails startup on purpose: it means
 * MI_FITNESS_TOKEN_KEY is wrong, and silently overwriting a rotated token is
 * worse than not starting.
 */
inline bool seed_xiaomi_credentials_if_missing() {
    if (!Config::is_initialized()) {
        return false;
    }
    auto& cfg = Config::get();
    const std::string token_key = cfg.get<std::string>("fitness.xiaomi.token_key", "MI_FITNESS_TOKEN_KEY", "");
    const std::string user_id = cfg.get<std::string>("fitness.xiaomi.user_id", "MI_FITNESS_USER_ID", "");
    const std::string pass_token = cfg.get<std::string>("fitness.xiaomi.pass_token", "MI_FITNESS_PASS_TOKEN", "");
    const std::string region = cfg.get<std::string>("fitness.xiaomi.region", "MI_FITNESS_REGION", "cn");
    const bool reseed = cfg.get<bool>("fitness.xiaomi.reseed", "MI_FITNESS_RESEED", false);

    if (token_key.empty() || user_id.empty() || pass_token.empty()) {
        spdlog::info("xiaomi: credential seeding skipped, seed values are not configured");
        return false;
    }

    CredentialsRepository repository(token_key);
    if (!reseed && repository.load().has_value()) {
        return false;
    }
    repository.store({user_id, pass_token, region});
    spdlog::info("xiaomi: credentials seeded for account {} (reseed={})", Xiaomi::mask_account_id(user_id), reseed);
    return true;
}

}  // namespace Repositories
