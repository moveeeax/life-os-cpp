/**
 * @file QrLogin.hpp
 * @brief Xiaomi sign-in by QR code for the Mi Fitness service id.
 *
 * The user confirms the sign-in in a Xiaomi app (by scanning the QR, or by
 * opening the confirmation link on the phone that has the app), and Xiaomi
 * answers a long poll with the account id and a passToken. No password
 * passes through this service.
 *
 * Protocol reference: the QR flow of PiotrMachowski/Xiaomi-cloud-tokens-
 * extractor (MIT), run here with sid=miothealth. Every URL Xiaomi returns is
 * checked against the same host allow-list as the login redirect: the long
 * poll carries session cookies, and the answer carries a token.
 */

#pragma once

#include <optional>
#include <string>

#include "fitness/xiaomi/HttpTransport.hpp"

namespace Xiaomi::QrLogin {

/// One sign-in attempt, as Xiaomi issued it.
struct Attempt {
    /// PNG bytes of the QR code.
    std::string qr_png;
    /// The page that confirms the sign-in; for a phone, where the QR on its
    /// own screen cannot be scanned.
    std::string confirm_url;
    /// Long-poll URL. Server side only: whoever polls it gets the token.
    std::string poll_url;
    /// Cookie header for the long poll.
    std::string cookies;
    /// How long Xiaomi keeps the attempt, in seconds.
    long timeout_seconds = 0;
};

/**
 * @brief Ask Xiaomi for a QR sign-in.
 * @throws MiFitnessProtocolError when Xiaomi is unreachable or answers in an
 *         unexpected shape.
 * @throws MiFitnessAuthError when a returned URL points outside xiaomi.com / mi.com.
 */
Attempt start(HttpTransport& transport);

/// The account the user confirmed.
struct Confirmed {
    std::string user_id;
    std::string pass_token;
};

/**
 * @brief One long-poll request with a short time limit.
 * @returns nullopt while the user has not confirmed (the request timed out
 *          or Xiaomi answered without a result).
 * @throws MiFitnessAuthError when the answer is malformed, declined, or
 *         points outside the allow-list. The attempt is then over.
 * @throws MiFitnessProtocolError on a network failure other than a timeout.
 */
std::optional<Confirmed> poll(HttpTransport& transport, const Attempt& attempt, long timeout_seconds = 3);

}  // namespace Xiaomi::QrLogin
