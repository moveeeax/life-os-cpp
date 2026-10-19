/**
 * @file S3Presign.hpp
 * @brief AWS Signature Version 4 presigned URLs for an S3-compatible store:
 *        the browser PUTs a wallpaper straight to the bucket, and the API
 *        sends its own HEAD and DELETE to short-lived presigned URLs, so one
 *        signing path serves both. The payload is never signed
 *        (UNSIGNED-PAYLOAD).
 */

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Storage::S3 {

struct Credentials {
    std::string access_key_id;
    std::string secret_access_key;
    std::string region;
};

struct Target {
    std::string endpoint;  // "https://storage.example.com", no trailing slash
    std::string bucket;
    bool path_style = true;  // endpoint/bucket/key; false: bucket.endpoint-host/key
};

struct Presign {
    std::string method;  // PUT | GET | HEAD | DELETE
    std::string key;     // object key, unencoded
    long expires_seconds = 900;
    std::string amz_date;                                      // "20130524T000000Z"
    std::vector<std::pair<std::string, std::string>> headers;  // signed besides host; lower-case names
    std::vector<std::pair<std::string, std::string>> query;    // extra signed query, e.g. response-content-disposition
};

/// Percent-encoding of SigV4: unreserved characters stay, everything else is %XX upper-case.
std::string uri_encode(std::string_view s, bool keep_slash);

/// The current UTC time as "YYYYMMDDTHHMMSSZ".
std::string amz_date_now();

/// The presigned URL; the caller sends exactly the signed headers with the request.
std::string presign(const Credentials& c, const Target& t, const Presign& p);

}  // namespace Storage::S3
