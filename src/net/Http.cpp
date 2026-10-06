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

std::size_t append_body(const char* data, std::size_t size, std::size_t nmemb, void* user) {
    static_cast<std::string*>(user)->append(data, size * nmemb);
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

Transport& transport() {
    if (override_slot() != nullptr) {
        return *override_slot();
    }
    static CurlTransport production;
    return production;
}

void install_for_testing(Transport* transport) {
    override_slot() = transport;
}

}  // namespace Net::Http
