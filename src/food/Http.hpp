/**
 * @file Http.hpp
 * @brief The HTTP seam of the food module: one GET at a time, scripted in
 *        tests, libcurl in production.
 */

#pragma once

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Food::Http {

struct Response {
    long status = 0;
    std::string body;
};

/// The request did not complete: DNS, connection, timeout.
struct TransportError : std::runtime_error {
    explicit TransportError(const std::string& what) : std::runtime_error(what) {}
};

class Transport {
public:
    Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    virtual ~Transport() = default;

    /// @throws TransportError when no response arrived.
    virtual Response get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers) = 0;

    /// POST a JSON body. @throws TransportError when no response arrived.
    virtual Response post_json(const std::string& url,
                               const std::string& body,
                               const std::vector<std::pair<std::string, std::string>>& headers,
                               long timeout_seconds) = 0;
};

class CurlTransport : public Transport {
public:
    explicit CurlTransport(long timeout_seconds = 10) : timeout_seconds_(timeout_seconds) {}
    Response get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers) override;
    Response post_json(const std::string& url,
                       const std::string& body,
                       const std::vector<std::pair<std::string, std::string>>& headers,
                       long timeout_seconds) override;

private:
    long timeout_seconds_;
};

/// The transport the food module uses: CurlTransport until a test overrides it.
Transport& transport();

/// Override in tests. nullptr restores the production one.
void install_for_testing(Transport* transport);

}  // namespace Food::Http
