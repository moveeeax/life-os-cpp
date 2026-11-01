/**
 * @file Settings.hpp
 * @brief The github module's constants and the OAuth App settings
 *        (github.client_id, github.client_secret, github.redirect_url,
 *        github.token_key). Header-only: the controller, the OAuth round and
 *        the sync job read the same values.
 */

#pragma once

#include <optional>
#include <string>

#include "utils/Config.hpp"
#include "utils/ConfigKeys.hpp"

namespace Github {

/// The worker job that pulls every linked user's repositories (src/github/jobs/).
inline constexpr const char* kSyncJob = "github_sync";
/// The schedule of the job in the API pod, and the least gap between two "Sync now".
inline constexpr int kSyncEveryHours = 6;
inline constexpr int kSyncMinSeconds = 60;
/// The OAuth scopes asked: private repositories, issues, PRs and checks; org membership.
inline constexpr const char* kScope = "repo read:org";
inline constexpr const char* kAuthorizeUrl = "https://github.com/login/oauth/authorize";
inline constexpr const char* kTokenUrl = "https://github.com/login/oauth/access_token";
inline constexpr const char* kApiUrl = "https://api.github.com";
inline constexpr const char* kGraphqlUrl = "https://api.github.com/graphql";
/// The seconds a one-time OAuth state lives in Redis (repositories/github/LinkRepository.hpp).
inline constexpr int kStateSeconds = 600;

struct Settings {
    std::string client_id;
    std::string client_secret;
    std::string redirect_url;
    /// The seal key of the users' tokens (utils/Seal.hpp).
    std::string token_key;
};

/// The settings; nullopt while the client id, secret, callback or key is missing (github_not_configured).
inline std::optional<Settings> settings() {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    auto& cfg = Config::get();
    Settings s;
    s.client_id = cfg.get<std::string>(Config::Keys::github_client_id, "");
    s.client_secret = cfg.get<std::string>(Config::Keys::github_client_secret, "");
    s.redirect_url = cfg.get<std::string>(Config::Keys::github_redirect_url,
                                          "https://life-os.tarassov.me/api/v1/github/oauth/callback");
    s.token_key = cfg.get<std::string>(Config::Keys::github_token_key, "");
    if (s.client_id.empty() || s.client_secret.empty() || s.redirect_url.empty() || s.token_key.empty()) {
        return std::nullopt;
    }
    return s;
}

}  // namespace Github
