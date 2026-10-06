/**
 * @file Http.cpp
 * @brief Bodies for src/food/Http.hpp.
 */

#include "food/Http.hpp"

#include <cstddef>
#include <memory>
#include <string>

#include <curl/curl.h>

#include "utils/CurlInit.hpp"

namespace Food::Http {

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

Response CurlTransport::get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers) {
    Utils::ensure_curl_init();
    std::unique_ptr<CURL, void (*)(CURL*)> handle(::curl_easy_init(), ::curl_easy_cleanup);
    if (!handle) {
        throw TransportError("curl_easy_init failed");
    }
    ::curl_slist* list = nullptr;
    for (const auto& [name, value] : headers) {
        list = ::curl_slist_append(list, (name + ": " + value).c_str());
    }
    const std::unique_ptr<::curl_slist, void (*)(::curl_slist*)> guard(list, ::curl_slist_free_all);

    Response response;
    ::curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    ::curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, append_body);
    ::curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response.body);
    ::curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeout_seconds_);
    ::curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    ::curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    ::curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 3L);
    ::curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "");
    if (list != nullptr) {
        ::curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, list);
    }
    const CURLcode rc = ::curl_easy_perform(handle.get());
    if (rc != CURLE_OK) {
        throw TransportError(std::string("request failed: ") + ::curl_easy_strerror(rc));
    }
    ::curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &response.status);
    return response;
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

}  // namespace Food::Http
