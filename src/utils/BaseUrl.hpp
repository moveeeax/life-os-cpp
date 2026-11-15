/**
 * @file BaseUrl.hpp
 * @brief The site's own address, as the links it gives out start with.
 */

#pragma once

#include <string>

#include "utils/Config.hpp"

namespace Utils {

/// app.base_url (APP_BASE_URL) without trailing slashes; http://localhost:8080
/// when it is not set or the configuration is not loaded.
inline std::string base_url() {
    std::string base = "http://localhost:8080";
    if (::Config::is_initialized()) {
        base = ::Config::get().get<std::string>("app.base_url", "APP_BASE_URL", base);
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

}  // namespace Utils
