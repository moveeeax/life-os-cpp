/**
 * @file LinkService.hpp
 * @brief Linking a Xiaomi account to a user by QR sign-in.
 *
 * start() asks Xiaomi for a QR sign-in and parks the attempt in Redis under a
 * random id bound to the user; step() polls Xiaomi once and, when the user
 * has confirmed, verifies the new token with a real login, finds the
 * account's region and stores the link. The long-poll URL and the Xiaomi
 * cookies never leave the server: whoever polls that URL receives the token.
 */

#pragma once

#include <chrono>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "cache/Cache.hpp"
#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Credentials.hpp"
#include "fitness/xiaomi/Crypto.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/QrLogin.hpp"
#include "fitness/xiaomi/RegionDetect.hpp"
#include "fitness/xiaomi/Service.hpp"
#include "repositories/fitness/MiAccountRepository.hpp"

namespace Fitness::Link {

/// MI_FITNESS_TOKEN_KEY is not set: a token could not be stored.
struct NotConfigured : std::runtime_error {
    NotConfigured() : std::runtime_error("MI_FITNESS_TOKEN_KEY is not set") {}
};

/// Too many sign-in attempts were started by this user.
struct RateLimited : std::runtime_error {
    RateLimited() : std::runtime_error("too many link attempts") {}
};

/// The attempt is unknown, has expired, or belongs to another user.
struct LinkNotFound : std::runtime_error {
    LinkNotFound() : std::runtime_error("link attempt not found") {}
};

/// The attempt could not be parked (Redis is down).
struct StorageUnavailable : std::runtime_error {
    StorageUnavailable() : std::runtime_error("link attempt could not be stored") {}
};

inline constexpr int kMaxStartsPerWindow = 5;
inline constexpr long kStartWindowSeconds = 600;
/// Time limit of one long-poll request to Xiaomi inside step().
inline constexpr long kPollSeconds = 3;

struct Started {
    std::string link_id;
    std::string qr_png_base64;
    std::string confirm_url;
    long expires_in_seconds = 0;
};

enum class State { Pending, Linked, Failed };

struct Step {
    State state = State::Pending;
    /// Machine code of the failure; empty unless state is Failed.
    std::string error_code;
};

namespace detail {

inline std::string hex(std::string_view bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

inline std::string attempt_key(const std::string& link_id) {
    return "mi:link:" + link_id;
}

inline std::string today_utc() {
    const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    char out[16];
    std::snprintf(out,
                  sizeof(out),
                  "%04d-%02u-%02u",
                  static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()),
                  static_cast<unsigned>(ymd.day()));
    return out;
}

/// A link id is 32 lowercase hex characters; anything else is not looked up.
inline bool is_link_id(const std::string& value) {
    if (value.size() != 32) {
        return false;
    }
    for (const char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

}  // namespace detail

/**
 * @brief Start a QR sign-in for this user.
 * @throws NotConfigured, RateLimited, StorageUnavailable; MiFitnessProtocolError
 *         or MiFitnessAuthError when Xiaomi does not issue a usable sign-in.
 */
inline Started start(const std::string& owner_id) {
    if (Xiaomi::Service::token_key_b64().empty()) {
        throw NotConfigured();
    }
    auto& cache = Cache::get();
    const std::string rate_key = "mi:link:rate:" + owner_id;
    const long long starts = cache.incr(rate_key);
    if (starts == 1) {
        cache.expire(rate_key, kStartWindowSeconds);
    }
    if (starts > kMaxStartsPerWindow) {
        throw RateLimited();
    }

    const Xiaomi::QrLogin::Attempt attempt = Xiaomi::QrLogin::start(Xiaomi::Service::transport());

    Started started;
    started.link_id = detail::hex(Xiaomi::Crypto::random_bytes(16));
    started.qr_png_base64 = Xiaomi::Crypto::b64_encode(attempt.qr_png);
    started.confirm_url = attempt.confirm_url;
    started.expires_in_seconds = attempt.timeout_seconds;

    const nlohmann::json parked{{"owner_id", owner_id}, {"poll_url", attempt.poll_url}, {"cookies", attempt.cookies}};
    if (!cache.set(detail::attempt_key(started.link_id), parked.dump(), attempt.timeout_seconds)) {
        throw StorageUnavailable();
    }
    return started;
}

/**
 * @brief One step of waiting for the user's confirmation.
 *
 * Pending: not confirmed yet, call again. Linked: the account is stored and
 * verified. Failed: the attempt is over, error_code says why. The attempt is
 * removed on Linked and Failed.
 *
 * @throws LinkNotFound when the id is unknown, expired or another user's.
 */
inline Step step(const std::string& owner_id, const std::string& link_id) {
    if (!detail::is_link_id(link_id)) {
        throw LinkNotFound();
    }
    auto& cache = Cache::get();
    const std::string key = detail::attempt_key(link_id);
    const auto raw = cache.get(key);
    if (!raw.has_value()) {
        throw LinkNotFound();
    }
    const nlohmann::json parked = nlohmann::json::parse(*raw, nullptr, /*allow_exceptions=*/false);
    if (parked.is_discarded() || !parked.is_object() || parked.value("owner_id", std::string()) != owner_id) {
        throw LinkNotFound();
    }

    // Two tabs polling the same attempt must not both act on the confirmation.
    const std::string lock_key = "mi:link:lock:" + link_id;
    if (!cache.set_nx(lock_key, "1", std::chrono::seconds(30))) {
        return {State::Pending, {}};
    }
    struct Unlock {
        Cache::CacheManager& cache;
        const std::string& key;
        ~Unlock() { cache.del(key); }
    } unlock{cache, lock_key};

    auto& transport = Xiaomi::Service::transport();
    Xiaomi::QrLogin::Attempt attempt;
    attempt.poll_url = parked.value("poll_url", std::string());
    attempt.cookies = parked.value("cookies", std::string());

    std::optional<Xiaomi::QrLogin::Confirmed> confirmed;
    try {
        confirmed = Xiaomi::QrLogin::poll(transport, attempt, kPollSeconds);
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        spdlog::warn("mi link: sign-in answer rejected: {}", e.what());
        cache.del(key);
        return {State::Failed, "xiaomi_refused"};
    } catch (const Xiaomi::MiFitnessProtocolError& e) {
        // A network hiccup between us and Xiaomi: the attempt is still alive.
        spdlog::warn("mi link: poll failed: {}", e.what());
        return {State::Pending, {}};
    }
    if (!confirmed.has_value()) {
        return {State::Pending, {}};
    }

    // Confirmed: whatever happens next, this attempt is over.
    cache.del(key);
    try {
        Repositories::MiAccountRepository accounts(Xiaomi::Service::token_key_b64());
        // Before any request with the new token: is this account free for this user?
        accounts.ensure_linkable(owner_id, confirmed->user_id);

        Xiaomi::CloudClient client(transport, {confirmed->user_id, confirmed->pass_token, "cn"}, {});
        client.login();
        const std::optional<std::string> region = Xiaomi::detect_region(client, detail::today_utc());

        Xiaomi::Credentials credentials = client.credentials();
        credentials.region = region.value_or("cn");
        accounts.link(owner_id, credentials, region.has_value());
        spdlog::info("mi link: account {} linked, region {}{}",
                     Xiaomi::mask_account_id(credentials.user_id),
                     credentials.region,
                     region.has_value() ? "" : " (not detected)");
        return {State::Linked, {}};
    } catch (const Repositories::AccountLinkedElsewhere&) {
        return {State::Failed, "account_linked_elsewhere"};
    } catch (const Repositories::DifferentAccount&) {
        return {State::Failed, "different_account"};
    } catch (const Xiaomi::MiFitnessAuthError& e) {
        spdlog::warn("mi link: login with the new token was refused: {}", e.what());
        return {State::Failed, "xiaomi_refused"};
    } catch (const Xiaomi::MiFitnessProtocolError& e) {
        spdlog::warn("mi link: login with the new token failed: {}", e.what());
        return {State::Failed, "xiaomi_unavailable"};
    }
}

}  // namespace Fitness::Link
