/**
 * @file MerchantRepository.hpp
 * @brief What the person's merchants usually are (money_merchants): the
 *        category picked last, the account used last, how often. Fed by the
 *        ledger, read by the quick entry and the parse prompt.
 */

#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "money/Merchant.hpp"
#include "repositories/money/Errors.hpp"

namespace Repositories::Money {

class MerchantRepository {
public:
    static std::string key(const std::string& merchant) { return ::Money::merchant_key(merchant); }

    /// Merchants whose key starts with or contains the typed text, most used first.
    nlohmann::json suggest(const std::string& owner, const std::string& q, long limit) {
        return Database::get().execute_read([&](auto& txn) {
            auto r = txn.exec_params(
                "SELECT COALESCE(json_agg(t), '[]'::json) FROM (SELECT " + kColumns +
                    " FROM money_merchants WHERE owner_id = $1::uuid "
                    " AND ($2 = '' OR merchant_key LIKE '%' || $2 || '%' ESCAPE '\\') "
                    " ORDER BY (merchant_key LIKE $2 || '%' ESCAPE '\\') DESC, times DESC, last_seen DESC "
                    " LIMIT $3) t",
                owner,
                escape_like(key(q)),
                limit);
            return nlohmann::json::parse(r[0][0].template as<std::string>());
        });
    }

    /// The most used merchants, for the parse prompt.
    nlohmann::json top(const std::string& owner, long limit) { return suggest(owner, "", limit); }

    /// Change what a merchant means from now on. @throws NotFound, Invariant.
    nlohmann::json set_category(const std::string& owner,
                                const std::string& merchant_key,
                                const std::optional<std::string>& category_id) {
        return detail::translate_sql(
            [&] {
                return Database::get().execute_write([&](auto& txn) {
                    auto r = txn.exec_params(
                        "UPDATE money_merchants SET category_id = $3::uuid WHERE owner_id = $1::uuid AND merchant_key "
                        "= $2 "
                        "RETURNING row_to_json((SELECT t FROM (SELECT " +
                            kColumns + ") t))",
                        owner,
                        merchant_key,
                        category_id);
                    if (r.empty()) {
                        throw NotFound("money_merchant");
                    }
                    return nlohmann::json::parse(r[0][0].template as<std::string>());
                });
            },
            &detail::translate);
    }

    static constexpr const char* kColumnsRaw =
        "merchant_key, display_name, category_id, account_id, times, to_char(last_seen, 'YYYY-MM-DD') AS last_seen";

private:
    inline static const std::string kColumns = kColumnsRaw;

    static std::string escape_like(const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '%' || c == '_' || c == '\\') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out;
    }
};

}  // namespace Repositories::Money
