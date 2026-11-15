/**
 * @file S3Presign.cpp
 * @brief SigV4 query-string signing (the AWS "presigned URL" form).
 */

#include "storage/S3Presign.hpp"

#include <algorithm>
#include <ctime>

#include "utils/Crypto.hpp"
#include "utils/Strings.hpp"

namespace Storage::S3 {

namespace {

using Utils::Crypto::hmac_sha256;
using Utils::Crypto::sha256_hex;

}  // namespace

std::string uri_encode(std::string_view s, bool keep_slash) {
    return Utils::Strings::percent_encode(s, keep_slash);
}

std::string amz_date_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buf[17];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

std::string presign(const Credentials& c, const Target& t, const Presign& p) {
    const auto sep = t.endpoint.find("://");
    const std::string scheme = t.endpoint.substr(0, sep);
    const std::string base_host = t.endpoint.substr(sep + 3);
    const std::string host = t.path_style ? base_host : t.bucket + "." + base_host;
    const std::string uri =
        t.path_style ? "/" + t.bucket + "/" + uri_encode(p.key, true) : "/" + uri_encode(p.key, true);
    const std::string day = p.amz_date.substr(0, 8);
    const std::string scope = day + "/" + c.region + "/s3/aws4_request";

    std::vector<std::pair<std::string, std::string>> headers = p.headers;
    headers.emplace_back("host", host);
    std::sort(headers.begin(), headers.end());
    std::string canonical_headers;
    std::string signed_headers;
    for (const auto& [name, value] : headers) {
        canonical_headers += name + ":" + value + "\n";
        signed_headers += (signed_headers.empty() ? "" : ";") + name;
    }

    std::vector<std::pair<std::string, std::string>> query = {
        {"X-Amz-Algorithm", "AWS4-HMAC-SHA256"},
        {"X-Amz-Credential", c.access_key_id + "/" + scope},
        {"X-Amz-Date", p.amz_date},
        {"X-Amz-Expires", std::to_string(p.expires_seconds)},
        {"X-Amz-SignedHeaders", signed_headers},
    };
    query.insert(query.end(), p.query.begin(), p.query.end());
    std::sort(query.begin(), query.end());
    std::string qs;
    for (const auto& [k, v] : query) {
        qs += (qs.empty() ? "" : "&") + uri_encode(k, false) + "=" + uri_encode(v, false);
    }

    const std::string canonical =
        p.method + "\n" + uri + "\n" + qs + "\n" + canonical_headers + "\n" + signed_headers + "\nUNSIGNED-PAYLOAD";
    const std::string to_sign = "AWS4-HMAC-SHA256\n" + p.amz_date + "\n" + scope + "\n" + sha256_hex(canonical);
    std::string key = hmac_sha256("AWS4" + c.secret_access_key, day);
    key = hmac_sha256(key, c.region);
    key = hmac_sha256(key, "s3");
    key = hmac_sha256(key, "aws4_request");
    const std::string sig = hmac_sha256(key, to_sign);
    return scheme + "://" + host + uri + "?" + qs + "&X-Amz-Signature=" +
           Utils::Crypto::detail::bytes_to_hex(reinterpret_cast<const unsigned char*>(sig.data()), sig.size());
}

}  // namespace Storage::S3
