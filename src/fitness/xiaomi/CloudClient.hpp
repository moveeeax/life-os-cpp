/**
 * @file CloudClient.hpp
 * @brief Client for the closed Mi Fitness cloud: login and signed requests.
 *
 * Knows HTTP and signing, knows neither the domain nor the database. Record
 * normalization and storage live higher up the stack.
 */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Credentials.hpp"
#include "fitness/xiaomi/Errors.hpp"
#include "fitness/xiaomi/HttpTransport.hpp"

namespace Xiaomi {

/**
 * @brief Whether the post-login redirect URL is allowed.
 *
 * The rules mirror upstream: https only, only the xiaomi.com and mi.com domains
 * with their subdomains, no userinfo in the URL, no non-standard port, no
 * whitespace or control characters. Anything else is a token leak channel,
 * because the session cookies are sent to the redirect target.
 */
bool is_allowed_login_redirect(std::string_view url);

class CloudClient {
public:
    /**
     * @param transport seam to the network, outlives the client
     * @param credentials initial credentials
     * @param on_rotate called when Xiaomi issues a new passToken. An exception
     *        from the handler does not fail the login: the token is already
     *        issued, and the session must carry on even if persisting it failed.
     */
    CloudClient(HttpTransport& transport, Credentials credentials, std::function<void(const Credentials&)> on_rotate);

    /// Two-step login. Throws MiFitnessAuthError on any format deviation:
    /// silently carrying on with half the fields is not an option.
    void login();

    const Credentials& credentials() const { return credentials_; }

    /**
     * @brief Records of one data key over a date range, all pages at once.
     *
     * Pagination follows the next_key cursor until has_more=false. A repeated
     * cursor or exceeding the page ceiling gives MiFitnessProtocolError: an endless
     * loop is worse than a loud failure. A call before login() gives MiFitnessAuthError.
     */
    std::vector<nlohmann::json> fetch_key(std::string_view key,
                                          std::string_view start_date,
                                          std::string_view end_date,
                                          std::optional<std::string_view> region);

    /**
     * @brief Workouts over a range: a separate endpoint, limit 50, field
     *        sport_records. Cursor rules as in fetch_key.
     */
    std::vector<nlohmann::json> fetch_sport_records(std::string_view start_date, std::string_view end_date);

    /**
     * @brief Daily sleep reports of the own account over a range of wake-up dates.
     *
     * A separate aggregates endpoint with a stricter cursor: a non-string,
     * empty or repeated next_key is a protocol error, not a silent stop.
     * data_list must be an array.
     */
    std::vector<nlohmann::json> fetch_daily_sleep_reports(std::string_view start_date, std::string_view end_date);

    /**
     * @brief Signed POST to the cloud. Returns the envelope's result field.
     *
     * A non-zero code is a refusal: upstream authentication codes (401, 403, -6,
     * -10001) give MiFitnessAuthError, which the job queue does not retry, the
     * rest give MiFitnessProtocolError.
     */
    nlohmann::json post_signed(const std::string& base_url, std::string_view api_path, const nlohmann::json& payload);

    /// Pagination page ceiling (used starting from task 5).
    void set_max_pages(int max_pages) { max_pages_ = max_pages; }
    int max_pages() const { return max_pages_; }

    /// Retry backoff base in milliseconds. Zero in tests: otherwise every run
    /// with retries sleeps for seconds.
    void set_retry_backoff_base_ms(int base_ms) { retry_backoff_base_ms_ = base_ms; }

private:
    /// A single signed POST attempt, no retries.
    nlohmann::json post_signed_once(const std::string& base_url,
                                    std::string_view api_path,
                                    const nlohmann::json& payload);

    HttpTransport& transport_;
    Credentials credentials_;
    std::function<void(const Credentials&)> on_rotate_;
    std::string ssecurity_b64_;
    std::string cookies_;
    int max_pages_ = 200;
    int retry_backoff_base_ms_ = 500;
};

}  // namespace Xiaomi
