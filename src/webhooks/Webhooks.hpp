/**
 * @file Webhooks.hpp
 * @brief Outbound webhooks: POST an event to a subscriber URL, delivered through
 *        the job system so failures retry with backoff and end in the DLQ
 *        (at-least-once). Bodies are signed with HMAC-SHA256 so receivers can
 *        verify authenticity.
 *
 * This is the DELIVERY seam. WHICH urls receive WHICH events is app-specific —
 * add a subscriptions table + controller (mirror the api_keys feature) and call
 * Webhooks::send() for each subscriber.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>

#include <nlohmann/json.hpp>

#include "jobs/Jobs.hpp"
#include "utils/Crypto.hpp"
#include "utils/CurlInit.hpp"

namespace Webhooks {

using json = nlohmann::json;

inline constexpr const char* kJobType = "webhook.deliver";

namespace detail {

/// Lower-cased host of an http(s) URL, or "" if it isn't one. An IPv6
/// literal comes back without its brackets ("http://[::1]/" -> "::1").
inline std::string url_host(const std::string& url) {
    auto scheme = url.find("://");
    if (scheme == std::string::npos)
        return "";
    const std::string proto = url.substr(0, scheme);
    if (proto != "http" && proto != "https")
        return "";
    std::string rest = url.substr(scheme + 3);
    // userinfo@ precedes the host; a '@' after the authority is path data.
    const auto authority_end = rest.find_first_of("/?#");
    const auto at = rest.substr(0, authority_end).rfind('@');
    if (at != std::string::npos)
        rest = rest.substr(at + 1);
    std::string host;
    if (!rest.empty() && rest[0] == '[') {
        const auto close = rest.find(']');
        if (close == std::string::npos)
            return "";
        host = rest.substr(1, close - 1);
    } else {
        const auto end = rest.find_first_of("/:?#");
        host = end == std::string::npos ? rest : rest.substr(0, end);
    }
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
    return host;
}

/// Port of an http(s) URL: the explicit one, else 443 / 80 by scheme. 0 when
/// the URL is not http(s) or the port is not a number in 1..65535.
inline int url_port(const std::string& url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos)
        return 0;
    const std::string proto = url.substr(0, scheme);
    const int fallback = proto == "https" ? 443 : (proto == "http" ? 80 : 0);
    if (fallback == 0)
        return 0;
    std::string authority = url.substr(scheme + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    const auto at = authority.rfind('@');
    if (at != std::string::npos)
        authority = authority.substr(at + 1);
    std::string::size_type colon;
    if (!authority.empty() && authority[0] == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos)
            return 0;
        colon = authority.find(':', close);
    } else {
        colon = authority.find(':');
    }
    if (colon == std::string::npos || colon + 1 == authority.size())
        return fallback;
    int port = 0;
    for (std::size_t i = colon + 1; i < authority.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(authority[i])) || port > 65535)
            return 0;
        port = port * 10 + (authority[i] - '0');
    }
    return port >= 1 && port <= 65535 ? port : 0;
}

/// The URL without userinfo, query and fragment — what goes into logs and
/// error messages (a subscriber URL often carries a token in its query).
inline std::string redact_url(const std::string& url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos)
        return "<not a url>";
    const auto authority_start = scheme + 3;
    const auto authority_end = url.find_first_of("/?#", authority_start);
    std::string authority = url.substr(
        authority_start, authority_end == std::string::npos ? std::string::npos : authority_end - authority_start);
    const auto at = authority.rfind('@');
    if (at != std::string::npos)
        authority = authority.substr(at + 1);
    std::string path;
    if (authority_end != std::string::npos && url[authority_end] == '/') {
        const auto path_end = url.find_first_of("?#", authority_end);
        path = url.substr(authority_end, path_end == std::string::npos ? std::string::npos : path_end - authority_end);
    }
    return url.substr(0, authority_start) + authority + path;
}

/// True for an IPv4 address (host byte order) no webhook may reach: "this
/// network" 0/8, private 10/8, 172.16/12, 192.168/16, CGNAT 100.64/10,
/// loopback 127/8, link-local 169.254/16 (cloud metadata), IETF 192.0.0/24,
/// benchmarking 198.18/15, multicast 224/4 and reserved 240/4 (with the
/// broadcast address).
inline bool ipv4_is_blocked(std::uint32_t a) {
    const auto in = [a](std::uint32_t net, int bits) {
        const std::uint32_t mask = bits == 0 ? 0u : ~0u << (32 - bits);
        return (a & mask) == net;
    };
    return in(0x00000000u, 8) || in(0x0A000000u, 8) || in(0x64400000u, 10) || in(0x7F000000u, 8) ||
           in(0xA9FE0000u, 16) || in(0xAC100000u, 12) || in(0xC0000000u, 24) || in(0xC0A80000u, 16) ||
           in(0xC6120000u, 15) || in(0xE0000000u, 4) || in(0xF0000000u, 4);
}

/// True for an IPv6 address no webhook may reach: unspecified, loopback,
/// unique-local fc00::/7, link-local fe80::/10, multicast ff00::/8, and any
/// form that embeds an IPv4 address (mapped ::ffff:0:0/96, compatible ::/96,
/// NAT64 64:ff9b::/96) whose IPv4 part is blocked.
inline bool ipv6_is_blocked(const unsigned char (&b)[16]) {
    const auto embedded_v4 = [&b] {
        return (std::uint32_t{b[12]} << 24) | (std::uint32_t{b[13]} << 16) | (std::uint32_t{b[14]} << 8) |
               std::uint32_t{b[15]};
    };
    bool first_ten_zero = true;
    for (int i = 0; i < 10; ++i)
        first_ten_zero = first_ten_zero && b[i] == 0;
    if (first_ten_zero && b[10] == 0xff && b[11] == 0xff)
        return ipv4_is_blocked(embedded_v4());  // ::ffff:a.b.c.d
    if (first_ten_zero && b[10] == 0 && b[11] == 0)
        return true;  // ::, ::1 and the deprecated ::a.b.c.d — nothing public lives here
    if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xff && b[3] == 0x9b) {
        bool zero_mid = true;
        for (int i = 4; i < 12; ++i)
            zero_mid = zero_mid && b[i] == 0;
        if (zero_mid)
            return ipv4_is_blocked(embedded_v4());  // 64:ff9b::a.b.c.d
    }
    if ((b[0] & 0xfe) == 0xfc)
        return true;  // fc00::/7
    if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
        return true;  // fe80::/10
    if (b[0] == 0xff)
        return true;  // multicast
    return false;
}

/// True when @p literal is an IP address in a blocked range — or is not an
/// IP literal at all (fail closed: callers pass resolver output here).
inline bool ip_literal_is_blocked(const std::string& literal) {
    unsigned char bytes[16] = {};
    if (::inet_pton(AF_INET, literal.c_str(), bytes) == 1) {
        // inet_pton writes network byte order: bytes[0] is the first octet.
        const std::uint32_t a = (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) |
                                (std::uint32_t{bytes[2]} << 8) | std::uint32_t{bytes[3]};
        return ipv4_is_blocked(a);
    }
    if (::inet_pton(AF_INET6, literal.c_str(), bytes) == 1)
        return ipv6_is_blocked(bytes);
    return true;
}

/// True when @p host is an IPv4 or IPv6 literal (not a name to resolve).
inline bool is_ip_literal(const std::string& host) {
    unsigned char bytes[16] = {};
    return ::inet_pton(AF_INET, host.c_str(), bytes) == 1 || ::inet_pton(AF_INET6, host.c_str(), bytes) == 1;
}

/// Reject obviously-internal targets by name or literal, before any lookup.
/// resolve_public() below is the real check: it resolves the name and
/// refuses when ANY address it maps to is internal.
inline bool host_is_blocked(const std::string& host_in) {
    // "localhost." is the same name as "localhost" (fully qualified form).
    std::string host = host_in;
    while (!host.empty() && host.back() == '.')
        host.pop_back();
    if (host.empty() || host == "localhost" || host.ends_with(".localhost"))
        return true;
    std::string bare = host;
    if (bare.size() > 2 && bare.front() == '[' && bare.back() == ']')
        bare = bare.substr(1, bare.size() - 2);
    if (is_ip_literal(bare))
        return ip_literal_is_blocked(bare);
    return false;
}

/**
 * @brief Resolve @p host and return one address to connect to, as text.
 *        THROWS when the name does not resolve or when any of its addresses
 *        is internal (a public name with one private A record is refused,
 *        not filtered). The caller pins curl to the returned address with
 *        CURLOPT_RESOLVE, so curl's own lookup cannot answer differently
 *        (DNS rebinding between this check and the connect).
 */
inline std::string resolve_public(const std::string& host) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || res == nullptr)
        throw std::runtime_error("webhook: cannot resolve host " + host);
    std::unique_ptr<addrinfo, void (*)(addrinfo*)> guard(res, ::freeaddrinfo);
    std::string chosen;
    for (const addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
            continue;
        char buf[NI_MAXHOST] = {};
        if (::getnameinfo(ai->ai_addr, ai->ai_addrlen, buf, sizeof buf, nullptr, 0, NI_NUMERICHOST) != 0)
            continue;
        std::string addr(buf);
        // A link-local IPv6 answer may carry a zone ("fe80::1%eth0").
        if (const auto pct = addr.find('%'); pct != std::string::npos)
            addr.resize(pct);
        if (ip_literal_is_blocked(addr))
            throw std::runtime_error("webhook: host " + host + " resolves to an internal address");
        if (chosen.empty())
            chosen = addr;
    }
    if (chosen.empty())
        throw std::runtime_error("webhook: host " + host + " has no usable address");
    return chosen;
}

}  // namespace detail

/**
 * @brief Worker-side: deliver one webhook. THROWS on a blocked URL, transport
 *        error, or non-2xx response so Jobs drives retry/backoff/DLQ.
 */
inline json process_job(const json& payload) {
    const std::string url = payload.value("url", "");
    const std::string event = payload.value("event", "");
    const std::string body = payload.contains("body") ? payload["body"].dump() : "{}";
    const std::string secret = payload.value("secret", "");

    const std::string host = detail::url_host(url);
    if (host.empty())
        throw std::runtime_error("webhook: url is not http(s): " + detail::redact_url(url));
    if (detail::host_is_blocked(host))
        throw std::runtime_error("webhook: refusing to POST to internal host: " + host);
    const int port = detail::url_port(url);
    if (port == 0)
        throw std::runtime_error("webhook: bad port in " + detail::redact_url(url));
    // Resolve once, refuse internal answers, and pin curl to that address.
    std::string pin;
    if (!detail::is_ip_literal(host)) {
        const std::string addr = detail::resolve_public(host);
        const bool v6 = addr.find(':') != std::string::npos;
        pin = host + ":" + std::to_string(port) + ":" + (v6 ? "[" + addr + "]" : addr);
    }

    Utils::ensure_curl_init();
    std::unique_ptr<CURL, void (*)(CURL*)> h(curl_easy_init(), curl_easy_cleanup);
    if (!h)
        throw std::runtime_error("webhook: curl_easy_init failed");

    curl_slist* hdrs = nullptr;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    const std::string event_hdr = "X-Webhook-Event: " + event;
    hdrs = curl_slist_append(hdrs, event_hdr.c_str());
    std::string sig_hdr;
    if (!secret.empty()) {
        const std::string raw = Utils::Crypto::hmac_sha256(secret, body);
        const std::string hex =
            Utils::Crypto::detail::bytes_to_hex(reinterpret_cast<const unsigned char*>(raw.data()), raw.size());
        sig_hdr = "X-Webhook-Signature: sha256=" + hex;
        hdrs = curl_slist_append(hdrs, sig_hdr.c_str());
    }
    std::unique_ptr<curl_slist, void (*)(curl_slist*)> hdr_guard(hdrs, curl_slist_free_all);

    std::unique_ptr<curl_slist, void (*)(curl_slist*)> resolve_guard(nullptr, curl_slist_free_all);
    if (!pin.empty()) {
        resolve_guard.reset(curl_slist_append(nullptr, pin.c_str()));
        curl_easy_setopt(h.get(), CURLOPT_RESOLVE, resolve_guard.get());
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(h.get(), CURLOPT_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(h.get(), CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(h.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(h.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(h.get(), CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h.get(), CURLOPT_FOLLOWLOCATION, 0L);  // a redirect could bypass the SSRF check
    curl_easy_setopt(
        h.get(), CURLOPT_WRITEFUNCTION, +[](char*, size_t s, size_t n, void*) -> size_t { return s * n; });

    const CURLcode rc = curl_easy_perform(h.get());
    if (rc != CURLE_OK)
        throw std::runtime_error(std::string("webhook: transport error: ") + curl_easy_strerror(rc));
    long status = 0;
    curl_easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status >= 300)
        throw std::runtime_error("webhook: non-2xx response (" + std::to_string(status) + ") from " + host);

    return json{{"delivered", true}, {"status", status}, {"event", event}};
}

/**
 * @brief App-facing: enqueue a webhook for delivery. Best-effort — never throws.
 *        Webhooks REQUIRE the job queue (async retry/DLQ); if Jobs is down the
 *        event is dropped with a warning rather than blocking the caller.
 */
inline void send(const std::string& url, const std::string& event, const json& body, const std::string& secret = "") {
    if (!Jobs::is_initialized()) {
        spdlog::warn("webhook: jobs not initialized; dropping {} -> {}", event, detail::redact_url(url));
        return;
    }
    try {
        json payload = {{"url", url}, {"event", event}, {"body", body}};
        if (!secret.empty())
            payload["secret"] = secret;
        Jobs::get().submit(kJobType, payload);
    } catch (const std::exception& e) {
        spdlog::warn("webhook: enqueue {} -> {} failed ({})", event, detail::redact_url(url), e.what());
    }
}

}  // namespace Webhooks
