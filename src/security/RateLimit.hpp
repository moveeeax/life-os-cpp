/**
 * @file RateLimit.hpp
 * @brief Redis-backed SLIDING-window request rate limiter.
 * @details One atomic Lua script per check: ZREMRANGEBYSCORE + ZCARD + ZADD
 *          over a per-identity sorted set — a true trailing window, immune to
 *          the classic 2x burst at fixed-window boundaries.
 *          Fail-open: if Redis is unavailable, requests pass through with a
 *          warning log — we prefer availability over strict enforcement by
 *          default. Production deployments that need hard caps should set
 *          fail_open=false (rejects with 503 instead).
 *
 * Declarations only for the non-template bodies — they live in RateLimit.cpp
 * (compiled once into app_core; docs/ARCHITECTURE.md §4). The
 * Config/Decision structs and the Limiter class shape stay here.
 */

#pragma once

#include <string>
#include <unordered_set>
#include <utility>

#include <drogon/HttpRequest.h>

namespace Security::RateLimit {

// Scope controls how a request is bucketed:
//   Ip        — always by client IP.
//   IpOrUser  — by authenticated user if one is present, else by IP.
// An earlier draft exposed a third "user-only" scope, but in practice that
// lumped every anonymous caller into a single bucket — hostile for any route
// that allows unauthenticated traffic. We fold it into IpOrUser.
enum class Scope { Ip, IpOrUser };

Scope parse_scope(const std::string& s);

struct Config {
    bool enabled = false;
    int requests = 60;    // max per window
    int window_sec = 60;  // window size
    // Stricter tier for the public auth/account surface (login, register,
    // refresh, password-reset, token links). Those paths are auth-public, so
    // the general limiter's public_paths skip leaves them unthrottled — this
    // tier re-arms them with a tight per-IP cap.
    int protected_requests = 10;
    int protected_window_sec = 60;
    // Failure tiers: only failures are counted, and while an identity has
    // `*_requests` failures inside the trailing window its next attempt is
    // refused before any work is done.
    //   auth_fail   — credentials the auth advice rejected (an unknown API
    //                 key, a forged or broken JWT), per client IP. Checked
    //                 before the key lookup (a primary-DB transaction) and the
    //                 signature check.
    //   login_email — failed logins per email address, whatever the IP, so a
    //                 spread-out guessing run on one account is throttled too.
    int auth_fail_requests = 20;
    int auth_fail_window_sec = 60;
    int login_email_requests = 5;
    int login_email_window_sec = 900;
    Scope scope = Scope::IpOrUser;
    bool trust_proxy = false;                   // read X-Forwarded-For
    int trusted_proxy_count = 1;                // # of trusted hops appended to XFF (index from the right)
    bool fail_open = true;                      // allow on Redis error
    std::unordered_set<std::string> whitelist;  // IPs or user subjects
    std::unordered_set<std::string> public_paths;
    std::unordered_set<std::string> protected_paths;  // auth surface, limited despite being public
};

/// Which failure tier a check_failures / record_failure call is about.
enum class FailureTier { AuthFailure, LoginEmail };

struct Decision {
    bool allowed = true;
    int remaining = 0;
    int retry_after_sec = 0;
};

class Limiter {
public:
    explicit Limiter(Config cfg) : cfg_(std::move(cfg)) {}

    const Config& config() const { return cfg_; }

    // General tier: skips api.public_paths, buckets by ip_or_user.
    Decision check(const std::string& identity) const;

    // Stricter tier for the public auth/account surface (login, register,
    // refresh, password-reset, token links). Separate Redis key namespace
    // ("rl:auth:") so it counts independently from the general limiter — a
    // burst of logins doesn't consume a user's normal API budget and vice
    // versa.
    Decision check_protected(const std::string& identity) const;

    // Failure tiers (see Config::auth_fail_* / login_email_*). check_failures
    // only reads the count: allowed is false while the identity already has
    // the tier's budget of failures in the window. record_failure adds one
    // (the set is trimmed to the budget, so a flood cannot grow it).
    // Keys "rl:authfail:" and "rl:login:". The whitelist and fail_open apply
    // as in check(); a failed record is logged and dropped.
    Decision check_failures(FailureTier tier, const std::string& identity) const;
    void record_failure(FailureTier tier, const std::string& identity) const;

private:
    bool whitelisted(const std::string& identity) const;

    Decision check_window(const std::string& identity, int requests, int window_sec, const char* key_prefix) const;

    Decision fallback_(const std::string& reason, int requests) const;

    Config cfg_;
};

// ---------------------------------------------------------------------------
// Global singleton
// ---------------------------------------------------------------------------

void initialize();

bool is_initialized();

Limiter& get();

void shutdown();

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

std::string client_ip(const drogon::HttpRequestPtr& req, bool trust_proxy, int trusted_proxy_count = 1);

// Convenience: resolve trust_proxy / trusted_proxy_count straight from app
// config, so any caller (audit, request logging, …) gets the SAME trusted
// client-IP logic as the limiter — even when rate limiting itself is disabled
// (the limiter may not be initialized, but the config keys still apply). With
// trust_proxy=false this returns peerAddr (the proxy hop) rather than a
// spoofable, client-supplied X-Real-IP header.
std::string client_ip(const drogon::HttpRequestPtr& req);

std::string identity_for(const drogon::HttpRequestPtr& req, const Config& cfg);

// Always-by-IP identity for the protected tier. Login/register carry no
// authenticated principal, and we don't want a (possibly attacker-supplied,
// expired) cookie's subject to shift the bucket — the brute-force unit is the
// source IP regardless of the configured scope.
std::string ip_identity(const drogon::HttpRequestPtr& req, const Config& cfg);

// Identity of the login_email tier: "email:" + sha256 of the trimmed,
// lower-cased address (the key never carries the address itself, and case or
// spaces cannot open a second bucket).
std::string email_identity(const std::string& email);

}  // namespace Security::RateLimit
