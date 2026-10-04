/**
 * @file Errors.hpp
 * @brief Two classes of Xiaomi protocol failures, distinguished by behavior.
 *
 * The split is not cosmetic. MiFitnessAuthError means the cloud did not accept
 * the credentials: retrying is pointless, the job queue does not retry such an
 * error but surfaces it as one requiring intervention.
 * MiFitnessProtocolError means a format or pagination contract violation: a
 * sign that Xiaomi changed the closed interface, and it must be visible as a
 * separate metric.
 *
 * None of the messages may contain the token value, ssecurity or the full
 * account identifier: the error text ends up in logs.
 */

#pragma once

#include <stdexcept>
#include <string>

namespace Xiaomi {

struct MiFitnessAuthError : std::runtime_error {
    explicit MiFitnessAuthError(const std::string& what) : std::runtime_error(what) {}
};

struct MiFitnessProtocolError : std::runtime_error {
    explicit MiFitnessProtocolError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace Xiaomi
