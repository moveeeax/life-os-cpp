/**
 * @file QrLogin.cpp
 * @brief Bodies for src/fitness/xiaomi/QrLogin.hpp.
 */

#include "fitness/xiaomi/QrLogin.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "fitness/xiaomi/CloudClient.hpp"
#include "fitness/xiaomi/Credentials.hpp"
#include "fitness/xiaomi/Errors.hpp"

namespace Xiaomi::QrLogin {

namespace {

constexpr std::string_view kPrefix = "&&&START&&&";
constexpr std::string_view kServiceLoginUrl = "https://account.xiaomi.com/pass/serviceLogin?_json=true&sid=miothealth";
constexpr std::string_view kLoginUrlEndpoint = "https://account.xiaomi.com/longPolling/loginUrl";
// The QR endpoints are the ones the Xiaomi web sign-in page calls; they are
// asked as a browser would ask them.
constexpr std::string_view kUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0 Safari/537.36";
/// Upper bound for the lifetime Xiaomi reports (300 s when this was written).
constexpr long kMaxTimeoutSeconds = 900;
/// A QR image is a few kilobytes; anything far larger is not one.
constexpr std::size_t kMaxQrBytes = 512 * 1024;

std::string url_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0f]);
        }
    }
    return out;
}

/// Xiaomi returns the confirmation link with HTML-escaped ampersands.
std::string unescape_ampersands(std::string value) {
    static constexpr std::string_view kEscaped = "&amp;";
    for (auto at = value.find(kEscaped); at != std::string::npos; at = value.find(kEscaped, at + 1)) {
        value.replace(at, kEscaped.size(), "&");
    }
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

/// name=value pairs of every Set-Cookie header, appended to @p cookies.
void collect_cookies(const Headers& headers, std::string& cookies) {
    for (const auto& [name, value] : headers) {
        if (lowercase(name) != "set-cookie") {
            continue;
        }
        const std::string pair = value.substr(0, value.find(';'));
        if (pair.empty() || pair.find('=') == std::string::npos) {
            continue;
        }
        if (!cookies.empty()) {
            cookies += "; ";
        }
        cookies += pair;
    }
}

/// The JSON object behind Xiaomi's "&&&START&&&" prefix. @p what names the
/// step in the error; the body itself never reaches an error or a log.
template <typename Error>
nlohmann::json parse_prefixed(const std::string& body, const char* what) {
    if (body.rfind(kPrefix, 0) != 0) {
        throw Error(std::string("Xiaomi ") + what + " response has an unexpected shape");
    }
    nlohmann::json payload = nlohmann::json::parse(body.substr(kPrefix.size()), nullptr, /*allow_exceptions=*/false);
    if (payload.is_discarded() || !payload.is_object()) {
        throw Error(std::string("Xiaomi ") + what + " response is not a JSON object");
    }
    return payload;
}

std::string string_field(const nlohmann::json& payload, const char* key) {
    if (!payload.contains(key)) {
        return {};
    }
    const auto& value = payload.at(key);
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    return {};
}

HttpRequest get(std::string url, const std::string& cookies = {}) {
    HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    request.headers.emplace_back("User-Agent", std::string(kUserAgent));
    if (!cookies.empty()) {
        request.headers.emplace_back("Cookie", cookies);
    }
    return request;
}

/// A URL from Xiaomi that this service is about to call or to show.
std::string allowed_url(const nlohmann::json& payload, const char* key) {
    const std::string url = unescape_ampersands(string_field(payload, key));
    if (url.empty()) {
        throw MiFitnessProtocolError(std::string("Xiaomi QR response is missing a field: ") + key);
    }
    if (!is_allowed_login_redirect(url)) {
        throw MiFitnessAuthError(std::string("Xiaomi QR response points outside the allowed hosts: ") + key);
    }
    return url;
}

}  // namespace

Attempt start(HttpTransport& transport) {
    Attempt attempt;

    // Step 1: the service's callback, and the first cookies of the session.
    const HttpResponse service = transport.send(get(std::string(kServiceLoginUrl)));
    if (service.status != 200) {
        throw MiFitnessProtocolError("Xiaomi sign-in page answered HTTP " + std::to_string(service.status));
    }
    collect_cookies(service.headers, attempt.cookies);
    const std::string callback =
        string_field(parse_prefixed<MiFitnessProtocolError>(service.body, "sign-in"), "callback");
    if (callback.empty() || !is_allowed_login_redirect(callback)) {
        throw MiFitnessAuthError("Xiaomi sign-in callback is missing or points outside the allowed hosts");
    }

    // Step 2: the QR sign-in itself.
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const std::string query = "?_qrsize=480&qs=" + url_encode(url_encode("?sid=miothealth&_json=true")) +
                              "&callback=" + url_encode(callback) + "&_hasLogo=false&sid=miothealth" +
                              "&serviceParam=&_locale=en_GB&_dc=" + std::to_string(now_ms);
    const HttpResponse issued = transport.send(get(std::string(kLoginUrlEndpoint) + query, attempt.cookies));
    if (issued.status != 200) {
        throw MiFitnessProtocolError("Xiaomi QR sign-in answered HTTP " + std::to_string(issued.status));
    }
    collect_cookies(issued.headers, attempt.cookies);
    const nlohmann::json payload = parse_prefixed<MiFitnessProtocolError>(issued.body, "QR sign-in");
    if (!payload.contains("code") || !payload["code"].is_number_integer() || payload["code"].get<long long>() != 0) {
        throw MiFitnessProtocolError("Xiaomi did not issue a QR sign-in");
    }
    const std::string qr_url = allowed_url(payload, "qr");
    attempt.confirm_url = allowed_url(payload, "loginUrl");
    attempt.poll_url = allowed_url(payload, "lp");
    const long timeout = payload.contains("timeout") && payload["timeout"].is_number_integer()
                             ? static_cast<long>(payload["timeout"].get<long long>())
                             : 0;
    if (timeout <= 0) {
        throw MiFitnessProtocolError("Xiaomi QR sign-in has no lifetime");
    }
    attempt.timeout_seconds = std::min(timeout, kMaxTimeoutSeconds);

    // Step 3: the image. It is handed to the browser by this service, so the
    // browser never talks to Xiaomi for it.
    const HttpResponse image = transport.send(get(qr_url, attempt.cookies));
    if (image.status != 200 || image.body.empty() || image.body.size() > kMaxQrBytes) {
        throw MiFitnessProtocolError("Xiaomi QR image could not be fetched");
    }
    attempt.qr_png = image.body;
    return attempt;
}

std::optional<Confirmed> poll(HttpTransport& transport, const Attempt& attempt, long timeout_seconds) {
    // Checked again here: the URL comes back from storage between requests.
    if (!is_allowed_login_redirect(attempt.poll_url)) {
        throw MiFitnessAuthError("Xiaomi long-poll URL points outside the allowed hosts");
    }
    HttpRequest request = get(attempt.poll_url, attempt.cookies);
    request.timeout_seconds = timeout_seconds;

    HttpResponse response;
    try {
        response = transport.send(request);
    } catch (const MiFitnessTimeoutError&) {
        return std::nullopt;  // nobody confirmed within this request
    }
    if (response.status != 200) {
        return std::nullopt;  // Xiaomi closed this round without a result
    }

    const nlohmann::json payload = parse_prefixed<MiFitnessAuthError>(response.body, "QR confirmation");
    if (!payload.contains("code") || !payload["code"].is_number_integer() || payload["code"].get<long long>() != 0) {
        throw MiFitnessAuthError("Xiaomi declined the QR sign-in");
    }
    Confirmed confirmed;
    confirmed.user_id = string_field(payload, "userId");
    confirmed.pass_token = string_field(payload, "passToken");
    if (confirmed.user_id.empty() || confirmed.pass_token.empty()) {
        throw MiFitnessAuthError("Xiaomi QR confirmation is missing the account or the token");
    }
    // Both values go into a Cookie header on every later login.
    validate_pass_token(confirmed.user_id);
    validate_pass_token(confirmed.pass_token);
    const std::string location = string_field(payload, "location");
    if (!location.empty() && !is_allowed_login_redirect(location)) {
        throw MiFitnessAuthError("Xiaomi QR confirmation points outside the allowed hosts");
    }
    return confirmed;
}

}  // namespace Xiaomi::QrLogin
