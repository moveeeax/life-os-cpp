/**
 * @file CurlTransport.hpp
 * @brief Production transport on libcurl.
 *
 * A thin adapter with no logic: all response validation lives in CloudClient
 * and is covered by tests against a fake. Nothing to test here except the curl
 * settings, and those are confirmed by a live request at rollout.
 *
 * Deliberately does not follow redirects: the client validates the target. An
 * automatic hop would send the session cookies anywhere.
 */

#pragma once

#include <string>

#include "fitness/xiaomi/HttpTransport.hpp"

namespace Xiaomi {

class CurlTransport : public HttpTransport {
public:
    explicit CurlTransport(long timeout_seconds = 20) : timeout_seconds_(timeout_seconds) {}

    HttpResponse send(const HttpRequest& request) override;

    /// Per-request limit in seconds: for tests and diagnostics.
    long timeout_seconds() const { return timeout_seconds_; }

private:
    long timeout_seconds_;
};

}  // namespace Xiaomi
