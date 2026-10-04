/**
 * @file HttpTransport.hpp
 * @brief Seam between the cloud client and the network.
 *
 * The client talks only to this interface, so all Xiaomi response parsing is
 * tested without the network. In production CurlTransport stands behind the
 * interface, in tests a fake with canned responses.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Xiaomi {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct HttpRequest {
    std::string method;
    std::string url;
    std::string body;
    Headers headers;
};

struct HttpResponse {
    long status = 0;
    std::string body;
    /// Names arrive as-is from the server, so they must be compared
    /// case-insensitively. A list, not a map: Set-Cookie repeats.
    Headers headers;
};

class HttpTransport {
public:
    HttpTransport() = default;
    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;
    HttpTransport(HttpTransport&&) = delete;
    HttpTransport& operator=(HttpTransport&&) = delete;
    virtual ~HttpTransport() = default;

    /// Must not follow redirects: the client validates the target itself.
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

}  // namespace Xiaomi
