/**
 * @file CloudClient.cpp
 * @brief Cloud client bodies: login and redirect URL validation.
 */

#include "fitness/xiaomi/CloudClient.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/Crypto.hpp"
#include "fitness/xiaomi/Regions.hpp"

namespace Xiaomi {

namespace {

constexpr std::string_view kLoginPrefix = "&&&START&&&";

// Without a User-Agent the cloud answers with an envelope carrying passToken:null
// (live diagnostics, 2026-09-29). The value is a known-good one: the default
// httpx UA the Python bridge worked with.
constexpr std::string_view kUserAgent = "python-httpx/0.28.1";
constexpr std::string_view kLoginUrl = "https://account.xiaomi.com/pass/serviceLogin?_json=true&sid=miothealth";

bool host_is_xiaomi(const std::string& host) {
    static constexpr std::string_view kDomains[] = {"xiaomi.com", "mi.com"};
    for (const auto& domain : kDomains) {
        if (host == domain) {
            return true;
        }
        if (host.size() > domain.size() + 1 && host.compare(host.size() - domain.size(), domain.size(), domain) == 0 &&
            host[host.size() - domain.size() - 1] == '.') {
            return true;
        }
    }
    return false;
}

std::string lowercase(std::string value) {
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

/// Header values matched by case-insensitive name. Returns every match:
/// Set-Cookie arrives more than once.
std::vector<std::string> header_values(const Headers& headers, std::string_view name) {
    std::vector<std::string> out;
    const std::string wanted = lowercase(std::string(name));
    for (const auto& [key, value] : headers) {
        if (lowercase(key) == wanted) {
            out.push_back(value);
        }
    }
    return out;
}

/// Value of a field Xiaomi returns either as a string or as a number (userId).
std::string field_as_string(const nlohmann::json& payload, const char* key) {
    const auto& value = payload.at(key);
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    throw MiFitnessAuthError(std::string("Xiaomi login response field has an unexpected type: ") + key);
}

}  // namespace

bool is_allowed_login_redirect(std::string_view url) {
    if (url.empty()) {
        return false;
    }
    for (const char c : url) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte == 0x7f) {
            return false;  // whitespace and control characters
        }
    }

    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos || url.substr(0, scheme_end) != "https") {
        return false;
    }

    const std::string_view rest = url.substr(scheme_end + 3);
    const auto authority_end = rest.find_first_of("/?#");
    const std::string_view authority = authority_end == std::string_view::npos ? rest : rest.substr(0, authority_end);
    if (authority.empty()) {
        return false;
    }
    // Userinfo in the URL is rejected: with it the redirect target is no longer
    // what it looks like.
    if (authority.find('@') != std::string_view::npos) {
        return false;
    }

    std::string host(authority);
    const auto colon = host.find(':');
    if (colon != std::string::npos) {
        if (host.substr(colon + 1) != "443") {
            return false;
        }
        host.erase(colon);
    }
    return host_is_xiaomi(lowercase(host));
}

CloudClient::CloudClient(HttpTransport& transport,
                         Credentials credentials,
                         std::function<void(const Credentials&)> on_rotate)
    : transport_(transport), credentials_(std::move(credentials)), on_rotate_(std::move(on_rotate)) {}

void CloudClient::login() {
    validate_pass_token(credentials_.pass_token);

    HttpRequest request;
    request.method = "GET";
    request.url = std::string(kLoginUrl);
    request.headers.emplace_back("User-Agent", std::string(kUserAgent));
    request.headers.emplace_back("Cookie", "userId=" + credentials_.user_id + "; passToken=" + credentials_.pass_token);

    const HttpResponse response = transport_.send(request);
    // Status before parsing the body: a transient 5xx must not look like dead
    // credentials, and the job queue does not retry an auth refusal (review finding 1).
    if (response.status == 401 || response.status == 403) {
        throw MiFitnessAuthError("Xiaomi login was refused: HTTP " + std::to_string(response.status));
    }
    if (response.status != 200) {
        throw MiFitnessProtocolError("Xiaomi login failed: HTTP " + std::to_string(response.status));
    }
    if (response.body.rfind(kLoginPrefix, 0) != 0) {
        // The body never goes into the error text: it may carry pieces of credentials.
        throw MiFitnessAuthError("Xiaomi login response is missing the &&&START&&& prefix");
    }

    nlohmann::json payload;
    {
        const std::string raw = response.body.substr(kLoginPrefix.size());
        payload = nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/false);
        // Parsing without exceptions on purpose: the nlohmann message quotes a
        // piece of the input, and the input is the login response.
        if (payload.is_discarded() || !payload.is_object()) {
            throw MiFitnessAuthError("Xiaomi login response is not a JSON object");
        }
    }

    for (const char* key : {"passToken", "userId", "ssecurity", "location"}) {
        if (!payload.contains(key) || payload.at(key).is_null()) {
            throw MiFitnessAuthError(std::string("Xiaomi login response is missing a field: ") + key);
        }
    }

    const std::string rotated_token = field_as_string(payload, "passToken");
    const std::string user_id = field_as_string(payload, "userId");
    const std::string ssecurity = field_as_string(payload, "ssecurity");
    const std::string location = field_as_string(payload, "location");

    validate_pass_token(rotated_token);
    validate_pass_token(user_id);  // same constraints: the value goes into a Cookie
    // Throws if this is not base64: without ssecurity no request can be signed,
    // and silently keeping garbage only defers the failure to the first data request.
    static_cast<void>(Crypto::b64_decode(ssecurity));

    if (!is_allowed_login_redirect(location)) {
        throw MiFitnessAuthError("Xiaomi login redirect points outside the allowed hosts");
    }

    // State is updated before the redirect request: Xiaomi has already issued
    // the new token, and a network error on the next step must not lose it.
    const bool rotated = rotated_token != credentials_.pass_token;
    credentials_.pass_token = rotated_token;
    credentials_.user_id = user_id;
    ssecurity_b64_ = ssecurity;

    if (rotated && on_rotate_) {
        try {
            on_rotate_(credentials_);
        } catch (const std::exception& e) {
            // Persisting failed, but the token is already issued and works in this
            // session. Failing here would lose it entirely.
            spdlog::warn("failed to persist the rotated Xiaomi passToken: {}", e.what());
        }
    }

    HttpRequest redirect;
    redirect.method = "GET";
    redirect.url = location;
    redirect.headers.emplace_back("User-Agent", std::string(kUserAgent));
    const HttpResponse redirect_response = transport_.send(redirect);

    std::string cookies;
    bool has_service_token = false;
    for (const auto& value : header_values(redirect_response.headers, "set-cookie")) {
        const std::string pair = value.substr(0, value.find(';'));
        if (pair.rfind("serviceToken=", 0) == 0 && pair.size() > std::string("serviceToken=").size()) {
            has_service_token = true;
        }
        if (!cookies.empty()) {
            cookies += "; ";
        }
        cookies += pair;
    }
    if (!has_service_token) {
        throw MiFitnessAuthError("Xiaomi login response is missing a serviceToken cookie");
    }
    cookies_ = cookies;
}

namespace {

/// Percent-encoding for application/x-www-form-urlencoded: everything except
/// letters, digits and -_.~. Space becomes %20, not a plus: that is what
/// Python's urlencode does with the upstream settings.
std::string url_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
                                byte == '~';
        if (unreserved) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0f]);
        }
    }
    return out;
}

/// Upstream authentication codes. Such a refusal is not retried: a retry only
/// burns attempts, a fresh token is needed.
bool is_authentication_code(long long code) {
    return code == 401 || code == 403 || code == -6 || code == -10001;
}

/// Attempts per data request: same as the Python bridge.
constexpr int kRequestRetries = 3;

/// Internal marker for "transient failure, retry makes sense". Never escapes:
/// after the last attempt it turns into MiFitnessProtocolError.
class MiFitnessRetryableError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace

nlohmann::json CloudClient::post_signed(const std::string& base_url,
                                        std::string_view api_path,
                                        const nlohmann::json& payload) {
    if (ssecurity_b64_.empty() || cookies_.empty()) {
        throw MiFitnessAuthError("data request before a successful login");
    }
    // The cloud answers deep ranges unreliably: gateway 502/504 and curl
    // timeouts come interleaved with successes. Such failures are retried up
    // to kRequestRetries attempts with exponential backoff (capped at 4
    // seconds), as in the Python bridge. Auth refusals and envelope errors are
    // not retried: a retry would not change them.
    for (int attempt = 0;; ++attempt) {
        const bool last_attempt = attempt == kRequestRetries - 1;
        try {
            return post_signed_once(base_url, api_path, payload);
        } catch (const MiFitnessRetryableError& e) {
            if (last_attempt) {
                throw MiFitnessProtocolError(e.what());
            }
        }
        const auto backoff = std::min<long long>(4000, static_cast<long long>(retry_backoff_base_ms_) << attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
    }
}

nlohmann::json CloudClient::post_signed_once(const std::string& base_url,
                                             std::string_view api_path,
                                             const nlohmann::json& payload) {
    const std::string data = payload.dump();
    const auto minutes =
        std::chrono::duration_cast<std::chrono::minutes>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string nonce = Crypto::make_nonce(minutes, Crypto::random_bytes(8));
    const std::string signed_nonce = Crypto::signed_nonce(ssecurity_b64_, nonce);

    // The signature is computed twice: before encryption over the plain form
    // and after it over the encrypted values. The order is mandatory and is
    // checked by the golden vectors.
    const std::string rc4_hash = Crypto::signature("POST", api_path, data, std::nullopt, signed_nonce);
    const std::string encrypted_data = Crypto::b64_encode(Crypto::rc4(signed_nonce, data));
    const std::string encrypted_hash = Crypto::b64_encode(Crypto::rc4(signed_nonce, rc4_hash));
    const std::string signature = Crypto::signature("POST", api_path, encrypted_data, encrypted_hash, signed_nonce);

    HttpRequest request;
    request.method = "POST";
    request.url = base_url + std::string(api_path);
    request.body = "data=" + url_encode(encrypted_data) + "&rc4_hash__=" + url_encode(encrypted_hash) +
                   "&signature=" + url_encode(signature) + "&_nonce=" + url_encode(Crypto::b64_encode(nonce));
    request.headers.emplace_back("User-Agent", std::string(kUserAgent));
    request.headers.emplace_back("Cookie", cookies_);
    request.headers.emplace_back("Content-Type", "application/x-www-form-urlencoded");

    HttpResponse response;
    try {
        response = transport_.send(request);
    } catch (const MiFitnessProtocolError& e) {
        // Transport level: a curl timeout or network error. No response
        // arrived, a retry makes sense.
        throw MiFitnessRetryableError(e.what());
    }
    if (response.status == 401 || response.status == 403) {
        throw MiFitnessAuthError("Xiaomi data request was refused: HTTP " + std::to_string(response.status));
    }
    if (response.status == 429 || response.status >= 500) {
        throw MiFitnessRetryableError("Xiaomi data request failed: HTTP " + std::to_string(response.status));
    }
    if (response.status != 200) {
        throw MiFitnessProtocolError("Xiaomi data request failed: HTTP " + std::to_string(response.status));
    }

    const std::string plaintext = Crypto::rc4(signed_nonce, Crypto::b64_decode(response.body));
    const nlohmann::json envelope = nlohmann::json::parse(plaintext, nullptr, /*allow_exceptions=*/false);
    if (envelope.is_discarded() || !envelope.is_object()) {
        // First sign of a change in the closed format: decrypted but not JSON,
        // or did not decrypt at all.
        throw MiFitnessProtocolError("Xiaomi response did not decrypt to a JSON object");
    }

    const long long code =
        envelope.contains("code") && envelope["code"].is_number_integer() ? envelope["code"].get<long long>() : -1;
    if (code != 0) {
        // The message text is server-controlled and stays out of the error: the
        // code is enough for diagnostics, and a foreign string has no place in the logs.
        if (is_authentication_code(code)) {
            throw MiFitnessAuthError("Xiaomi refused authentication, code " + std::to_string(code));
        }
        throw MiFitnessProtocolError("Xiaomi returned error code " + std::to_string(code));
    }
    const nlohmann::json result = envelope.value("result", nlohmann::json::object());
    if (!result.is_object()) {
        // A shape mismatch is a change in the closed format, not a bare
        // nlohmann exception outside the retry taxonomy (review finding 3).
        throw MiFitnessProtocolError("Xiaomi envelope result is not an object");
    }
    return result;
}

std::vector<nlohmann::json> CloudClient::fetch_key(std::string_view key,
                                                   std::string_view start_date,
                                                   std::string_view end_date,
                                                   std::optional<std::string_view> region) {
    const std::string region_name(region.value_or(std::string_view(credentials_.region)));
    const std::string base_url = host_for_region(region_name);
    const auto [start_time, end_time] = range_to_timestamps(start_date, end_date, region_name);

    std::vector<nlohmann::json> items;
    std::set<std::string> seen_cursors;
    std::optional<std::string> next_key;
    for (int page = 1;; ++page) {
        if (page > max_pages_) {
            throw MiFitnessProtocolError("Xiaomi pagination exceeded the page ceiling");
        }
        nlohmann::json payload{{"start_time", start_time}, {"end_time", end_time}, {"key", std::string(key)}};
        if (next_key.has_value()) {
            payload["next_key"] = *next_key;
        }
        const nlohmann::json result = post_signed(base_url, "/app/v1/data/get_fitness_data_by_time", payload);

        if (result.contains("data_list") && result["data_list"].is_array()) {
            for (const auto& item : result["data_list"]) {
                items.push_back(item);
            }
        }
        // has_more arrives both as bool and as a number: Python takes it by
        // truthiness, the port must do the same instead of failing with type_error.
        const bool has_more = [&result] {
            if (!result.contains("has_more")) {
                return false;
            }
            const auto& flag = result["has_more"];
            if (flag.is_boolean()) {
                return flag.get<bool>();
            }
            if (flag.is_number()) {
                return flag.get<double>() != 0.0;
            }
            return false;
        }();
        if (!has_more || !result.contains("next_key") || result["next_key"].is_null()) {
            break;
        }
        const auto& cursor_json = result["next_key"];
        const std::string cursor = cursor_json.is_string() ? cursor_json.get<std::string>() : cursor_json.dump();
        // An empty cursor is falsy in Python and ends pagination. An extra
        // request with an empty next_key would turn into a false loop error.
        if (cursor.empty()) {
            break;
        }
        // A repeated cursor is a loop, and failing beats spinning forever.
        if (!seen_cursors.insert(cursor).second) {
            throw MiFitnessProtocolError("Xiaomi pagination cursor loop detected");
        }
        next_key = cursor;
    }
    return items;
}

std::vector<nlohmann::json> CloudClient::fetch_daily_sleep_reports(std::string_view start_date,
                                                                   std::string_view end_date) {
    const std::string base_url = host_for_region(credentials_.region);
    const auto [start_time, end_time] = range_to_timestamps(start_date, end_date, credentials_.region);

    std::vector<nlohmann::json> items;
    std::set<std::string> seen_cursors;
    std::optional<std::string> next_key;
    for (int page = 1;; ++page) {
        if (page > max_pages_) {
            throw MiFitnessProtocolError("sleep report pagination exceeded the page ceiling");
        }
        nlohmann::json payload{{"key", "sleep"},
                               {"tag", "daily_report"},
                               {"limit", 100},
                               {"start_time", start_time},
                               {"end_time", end_time}};
        if (next_key.has_value()) {
            payload["next_key"] = *next_key;
        }
        const nlohmann::json result =
            post_signed(base_url, "/app/v1/data/get_aggregated_fitness_data_by_time", payload);
        if (!result.contains("data_list") || !result["data_list"].is_array()) {
            throw MiFitnessProtocolError("invalid daily sleep report response");
        }
        for (const auto& item : result["data_list"]) {
            items.push_back(item);
        }
        const bool has_more = result.contains("has_more") && result["has_more"].is_boolean()
                                  ? result["has_more"].get<bool>()
                                  : result.value("has_more", 0) != 0;
        if (!has_more) {
            return items;
        }
        // The report cursor is stricter than usual: a non-empty string only, no repeats.
        if (!result.contains("next_key") || !result["next_key"].is_string()) {
            throw MiFitnessProtocolError("invalid daily sleep report pagination cursor");
        }
        const std::string cursor = result["next_key"].get<std::string>();
        if (cursor.empty() || !seen_cursors.insert(cursor).second) {
            throw MiFitnessProtocolError("invalid daily sleep report pagination cursor");
        }
        next_key = cursor;
    }
}

std::vector<nlohmann::json> CloudClient::fetch_sport_records(std::string_view start_date, std::string_view end_date) {
    const std::string base_url = host_for_region(credentials_.region);
    const auto [start_time, end_time] = range_to_timestamps(start_date, end_date, credentials_.region);

    std::vector<nlohmann::json> items;
    std::set<std::string> seen_cursors;
    std::optional<std::string> next_key;
    for (int page = 1;; ++page) {
        if (page > max_pages_) {
            throw MiFitnessProtocolError("sport pagination exceeded the page ceiling");
        }
        nlohmann::json payload{{"start_time", start_time}, {"end_time", end_time}, {"limit", 50}};
        if (next_key.has_value()) {
            payload["next_key"] = *next_key;
        }
        const nlohmann::json result = post_signed(base_url, "/app/v1/data/get_sport_records_by_time", payload);
        if (result.contains("sport_records") && result["sport_records"].is_array()) {
            for (const auto& item : result["sport_records"]) {
                items.push_back(item);
            }
        }
        const bool has_more = [&result] {
            if (!result.contains("has_more")) {
                return false;
            }
            const auto& flag = result["has_more"];
            if (flag.is_boolean()) {
                return flag.get<bool>();
            }
            return flag.is_number() && flag.get<double>() != 0.0;
        }();
        if (!has_more || !result.contains("next_key") || result["next_key"].is_null()) {
            break;
        }
        const auto& cursor_json = result["next_key"];
        const std::string cursor = cursor_json.is_string() ? cursor_json.get<std::string>() : cursor_json.dump();
        if (cursor.empty()) {
            break;
        }
        if (!seen_cursors.insert(cursor).second) {
            throw MiFitnessProtocolError("sport pagination cursor loop detected");
        }
        next_key = cursor;
    }
    return items;
}

}  // namespace Xiaomi
