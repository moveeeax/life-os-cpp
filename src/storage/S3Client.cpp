/**
 * @file S3Client.cpp
 * @brief HEAD and DELETE over libcurl to presigned URLs, and the process-wide
 *        store built from storage.s3.*.
 */

#include "storage/S3Client.hpp"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <curl/curl.h>

#include "utils/CurlInit.hpp"

namespace Storage {

namespace {

constexpr long kUploadSeconds = 900;
constexpr long kOwnRequestSeconds = 60;
constexpr long kConnectTimeoutSeconds = 5;
constexpr long kTimeoutSeconds = 15;

struct Answer {
    long status = 0;
    long long length = -1;
};

/// One bodiless request; throws on a transport failure.
Answer send(const std::string& method, const std::string& url) {
    Utils::ensure_curl_init();
    CURL* h = curl_easy_init();
    if (h == nullptr) {
        throw std::runtime_error("s3: curl_easy_init failed");
    }
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    if (method == "HEAD") {
        curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
    } else {
        curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
    }
    curl_easy_setopt(h, CURLOPT_TIMEOUT, kTimeoutSeconds);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    // Multi-threaded process: no SIGALRM for libcurl's own timeouts.
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    // A DELETE answers a small XML body on errors; nobody reads it.
    curl_easy_setopt(
        h, CURLOPT_WRITEFUNCTION, +[](char*, std::size_t size, std::size_t n, void*) -> std::size_t {
            return size * n;
        });

    const CURLcode rc = curl_easy_perform(h);
    Answer a;
    if (rc == CURLE_OK) {
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &a.status);
        curl_off_t len = -1;
        curl_easy_getinfo(h, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
        a.length = static_cast<long long>(len);
    }
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        throw std::runtime_error(std::string("s3: transport error: ") + curl_easy_strerror(rc));
    }
    return a;
}

std::mutex g_store_mutex;
ObjectStore* g_test_store = nullptr;

}  // namespace

S3Client::S3Client(S3::Settings settings) : settings_(std::move(settings)) {}

std::string S3Client::signed_url(const std::string& method,
                                 const std::string& key,
                                 long expires_seconds,
                                 std::vector<std::pair<std::string, std::string>> headers,
                                 std::vector<std::pair<std::string, std::string>> query) const {
    return S3::presign(
        settings_.credentials,
        settings_.target,
        S3::Presign{method, key, expires_seconds, S3::amz_date_now(), std::move(headers), std::move(query)});
}

std::string S3Client::upload_url(const std::string& key, const std::string& content_type, long long bytes) const {
    return signed_url("PUT",
                      key,
                      kUploadSeconds,
                      {{"cache-control", kImmutableCacheControl},
                       {"content-length", std::to_string(bytes)},
                       {"content-type", content_type}});
}

ObjectInfo S3Client::head(const std::string& key) const {
    const Answer a = send("HEAD", signed_url("HEAD", key, kOwnRequestSeconds, {}));
    if (a.status == 200) {
        return ObjectInfo{true, a.length};
    }
    if (a.status == 404) {
        return ObjectInfo{false, 0};
    }
    throw std::runtime_error("s3: HEAD answered HTTP " + std::to_string(a.status));
}

void S3Client::remove(const std::string& key) const {
    const Answer a = send("DELETE", signed_url("DELETE", key, kOwnRequestSeconds, {}));
    if (a.status != 200 && a.status != 204 && a.status != 404) {
        throw std::runtime_error("s3: DELETE answered HTTP " + std::to_string(a.status));
    }
}

std::string S3Client::public_url(const std::string& key) const {
    return settings_.public_base_url + "/" + S3::uri_encode(key, true);
}

std::string S3Client::key_for(const std::string& folder, const std::string& id, const std::string& name) const {
    return settings_.prefix + folder + "/" + id + "/" + name;
}

std::string S3Client::private_key(const std::string& owner, const std::string& id, const std::string& name) const {
    return settings_.prefix + "private/" + owner + "/" + id + "/" + name;
}

std::string S3Client::private_upload_url(const std::string& key,
                                         const std::string& content_type,
                                         long long bytes) const {
    return signed_url("PUT",
                      key,
                      kUploadSeconds,
                      {{"cache-control", kPrivateCacheControl},
                       {"content-length", std::to_string(bytes)},
                       {"content-type", content_type}});
}

std::string S3Client::private_put_url(const std::string& key, long seconds) const {
    return signed_url("PUT", key, seconds, {{"cache-control", kPrivateCacheControl}});
}

std::string S3Client::download_url(const std::string& key, long seconds, const std::string& disposition) const {
    return signed_url("GET", key, seconds, {}, {{"response-content-disposition", disposition}});
}

bool S3Client::publicly_readable(const std::string& key) const {
    // No signature: this is the request anyone on the internet could send.
    const Answer a = send("HEAD", public_url(key));
    if (a.status >= 200 && a.status < 300) {
        return true;
    }
    if (a.status >= 400 && a.status < 500) {
        return false;
    }
    throw std::runtime_error("s3: anonymous HEAD answered HTTP " + std::to_string(a.status));
}

ObjectStore* object_store() {
    std::lock_guard<std::mutex> lock(g_store_mutex);
    if (g_test_store != nullptr) {
        return g_test_store;
    }
    // Built once on first use: the settings do not change while the process runs.
    static const std::unique_ptr<S3Client> configured = [] {
        auto s = S3::settings_from_config();
        return s ? std::make_unique<S3Client>(std::move(*s)) : std::unique_ptr<S3Client>{};
    }();
    return configured.get();
}

void install_object_store_for_testing(ObjectStore* store) {
    std::lock_guard<std::mutex> lock(g_store_mutex);
    g_test_store = store;
}

}  // namespace Storage
