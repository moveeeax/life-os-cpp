/**
 * @file Http.cpp
 * @brief Bodies for src/food/Http.hpp.
 */

#include "net/Http.hpp"

#include <cstddef>
#include <memory>
#include <string>

#include <curl/curl.h>

#include "utils/CurlInit.hpp"

namespace Net::Http {

namespace {

/// Largest response body any caller of this client needs (LLM answers, rate
/// tables, Open Food Facts records are all well under it).
constexpr std::size_t kMaxResponseBytes = 16 * 1024 * 1024;

std::size_t append_body(const char* data, std::size_t size, std::size_t nmemb, void* user) {
    auto* body = static_cast<std::string*>(user);
    // Returning less than handed over makes curl abort with CURLE_WRITE_ERROR:
    // a body past the cap (a server that streams without Content-Length)
    // never grows the string beyond it.
    if (body->size() + size * nmemb > kMaxResponseBytes)
        return 0;
    body->append(data, size * nmemb);
    return size * nmemb;
}

Transport*& override_slot() {
    static Transport* slot = nullptr;
    return slot;
}

}  // namespace

namespace {

/// One request with the common options; the caller sets the method.
Response perform(CURL* handle,
                 const std::string& url,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 long timeout_seconds) {
    ::curl_slist* list = nullptr;
    for (const auto& [name, value] : headers) {
        list = ::curl_slist_append(list, (name + ": " + value).c_str());
    }
    const std::unique_ptr<::curl_slist, void (*)(::curl_slist*)> guard(list, ::curl_slist_free_all);

    Response response;
    ::curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_body);
    ::curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
    ::curl_easy_setopt(handle, CURLOPT_TIMEOUT, timeout_seconds);
    ::curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    ::curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 3L);
    // Only http and https, for the first request and for every redirect: a
    // redirect to file:// or gopher:// must not be followed.
#if LIBCURL_VERSION_NUM >= 0x075500
    ::curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
    ::curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    ::curl_easy_setopt(handle, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    ::curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    // Refuse up front when the server announces a larger body.
    ::curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(kMaxResponseBytes));
    ::curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    if (list != nullptr) {
        ::curl_easy_setopt(handle, CURLOPT_HTTPHEADER, list);
    }
    const CURLcode rc = ::curl_easy_perform(handle);
    if (rc != CURLE_OK) {
        throw TransportError(std::string("request failed: ") + ::curl_easy_strerror(rc));
    }
    ::curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}

}  // namespace

Response CurlTransport::get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers) {
    Utils::ensure_curl_init();
    std::unique_ptr<CURL, void (*)(CURL*)> handle(::curl_easy_init(), ::curl_easy_cleanup);
    if (!handle) {
        throw TransportError("curl_easy_init failed");
    }
    return perform(handle.get(), url, headers, timeout_seconds_);
}

Response CurlTransport::post_json(const std::string& url,
                                  const std::string& body,
                                  const std::vector<std::pair<std::string, std::string>>& headers,
                                  long timeout_seconds) {
    Utils::ensure_curl_init();
    std::unique_ptr<CURL, void (*)(CURL*)> handle(::curl_easy_init(), ::curl_easy_cleanup);
    if (!handle) {
        throw TransportError("curl_easy_init failed");
    }
    auto with_type = headers;
    with_type.emplace_back("Content-Type", "application/json");
    ::curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
    ::curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.c_str());
    ::curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    return perform(handle.get(), url, with_type, timeout_seconds > 0 ? timeout_seconds : timeout_seconds_);
}

Response Transport::send(const Request& request) {
    throw TransportError("this transport does not send " + request.method + " requests");
}

Response CurlTransport::send(const Request& request) {
    Utils::ensure_curl_init();
    std::unique_ptr<CURL, void (*)(CURL*)> handle(::curl_easy_init(), ::curl_easy_cleanup);
    if (!handle) {
        throw TransportError("curl_easy_init failed");
    }
    auto headers = request.headers;
    if (!request.content_type.empty()) {
        headers.emplace_back("Content-Type", request.content_type);
    }
    if (request.method == "POST") {
        ::curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
    } else if (request.method != "GET") {
        ::curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }
    if (!request.body.empty() || request.method == "POST") {
        ::curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, request.body.c_str());
        ::curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }
    if (!request.ca_file.empty()) {
        ::curl_easy_setopt(handle.get(), CURLOPT_CAINFO, request.ca_file.c_str());
    }
    return perform(
        handle.get(), request.url, headers, request.timeout_seconds > 0 ? request.timeout_seconds : timeout_seconds_);
}

Transport& transport() {
    if (override_slot() != nullptr) {
        return *override_slot();
    }
    static CurlTransport production;
    return production;
}

Transport& quick_transport() {
    if (override_slot() != nullptr) {
        return *override_slot();
    }
    static CurlTransport quick(3);
    return quick;
}

void install_for_testing(Transport* transport) {
    override_slot() = transport;
}

}  // namespace Net::Http
