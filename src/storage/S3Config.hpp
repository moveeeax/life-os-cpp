/**
 * @file S3Config.hpp
 * @brief The storage.s3.* settings (docs/CONFIG.md): complete or nothing.
 */

#pragma once

#include <optional>
#include <string>

#include "storage/S3Presign.hpp"
#include "utils/Config.hpp"

namespace Storage::S3 {

struct Settings {
    Credentials credentials;
    Target target;
    std::string prefix;           // ends with '/' unless empty
    std::string public_base_url;  // no trailing '/'
};

/// nullopt while the endpoint, bucket or public base URL is empty.
inline std::optional<Settings> settings_from_config() {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    const auto& cfg = Config::get();
    Settings s{{cfg.get<std::string>("storage.s3.access_key_id", "S3_ACCESS_KEY_ID", ""),
                cfg.get<std::string>("storage.s3.secret_access_key", "S3_SECRET_ACCESS_KEY", ""),
                cfg.get<std::string>("storage.s3.region", "S3_REGION", "")},
               {cfg.get<std::string>("storage.s3.endpoint", "S3_ENDPOINT", ""),
                cfg.get<std::string>("storage.s3.bucket", "S3_BUCKET", ""),
                cfg.get<bool>("storage.s3.path_style", "S3_PATH_STYLE", true)},
               cfg.get<std::string>("storage.s3.prefix", "S3_PREFIX", "life-os/"),
               cfg.get<std::string>("storage.s3.public_base_url", "S3_PUBLIC_BASE_URL", "")};
    while (!s.target.endpoint.empty() && s.target.endpoint.back() == '/') {
        s.target.endpoint.pop_back();
    }
    while (!s.public_base_url.empty() && s.public_base_url.back() == '/') {
        s.public_base_url.pop_back();
    }
    if (!s.prefix.empty() && s.prefix.back() != '/') {
        s.prefix += '/';
    }
    if (s.target.endpoint.empty() || s.target.bucket.empty() || s.public_base_url.empty() ||
        s.target.endpoint.find("://") == std::string::npos) {
        return std::nullopt;
    }
    return s;
}

}  // namespace Storage::S3
