/**
 * @file UsedTokenRepository.hpp
 * @brief The used_tokens table (migration 002): one row per one-shot token
 *        already spent, kept until it would have expired anyway. The hourly
 *        used_tokens_reaper in core/Core.cpp deletes the expired rows.
 */

#pragma once

#include <string>

#include "database/Database.hpp"

namespace Repositories {

class UsedTokenRepository {
public:
    /// Record @p token_hash as spent for @p ttl_sec seconds. True the first time
    /// the hash is seen, false on every replay (INSERT ... ON CONFLICT DO NOTHING
    /// RETURNING). Throws on a database error; the caller decides how to fail.
    bool consume(const std::string& token_hash, int ttl_sec) {
        return Database::get().execute_write([&](auto& txn) {
            auto r = txn.exec_params(
                "INSERT INTO used_tokens (token_hash, expires_at) "
                "VALUES ($1, now() + make_interval(secs => $2)) "
                "ON CONFLICT (token_hash) DO NOTHING RETURNING token_hash",
                token_hash,
                ttl_sec);
            return !r.empty();  // a row came back: this is the first use
        });
    }
};

}  // namespace Repositories
