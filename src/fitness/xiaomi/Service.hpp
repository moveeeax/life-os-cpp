/**
 * @file Service.hpp
 * @brief Transport override point and access to the Xiaomi module settings.
 *
 * Mirrors the shape of the Billing and Storage test seams: a production
 * singleton by default, install_for_testing swaps it for a fake. The controller
 * takes the transport here and does not know whether it is real or a test one.
 */

#pragma once

#include <string>

#include "fitness/xiaomi/HttpTransport.hpp"

namespace Xiaomi::Service {

/// Transport to the cloud: CurlTransport until a test overrides it.
HttpTransport& transport();

/// Override the transport in tests. nullptr restores the production one.
void install_for_testing(HttpTransport* transport);

/// Token encryption key from the config (xiaomi.token_key / MI_FITNESS_TOKEN_KEY).
/// An empty string means "not configured", the caller decides.
std::string token_key_b64();

/// Per-request limit for one HTTP call to the cloud, in seconds
/// (xiaomi.http_timeout_seconds / MI_FITNESS_HTTP_TIMEOUT, default 20).
/// A deep backfill takes the cloud longer than 20 seconds, the knob raises the
/// limit without a rebuild. Returns the default before the config is initialized.
long http_timeout_seconds();

}  // namespace Xiaomi::Service
