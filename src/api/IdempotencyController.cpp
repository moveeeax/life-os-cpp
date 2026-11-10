/**
 * @file IdempotencyController.cpp
 * @brief Body for src/api/IdempotencyController.hpp — compiled once into
 *        app_core. The contract is documented on the declaration in the header.
 */

#include "api/IdempotencyController.hpp"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "security/Idempotency.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Strings.hpp"

namespace Api {

namespace {

constexpr std::size_t kKeyMaxLength = 200;

}  // namespace

void IdempotencyController::seen(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    API_REQUIRE_OWNER(req, callback, owner);
    (void)owner;  // the markers are scoped by the same principal inside seen()
    const std::vector<std::string> keys = Utils::Strings::split_csv_vec(req->getParameter("keys"));
    if (keys.empty() || keys.size() > Security::Idempotency::kSeenMax) {
        callback(ErrorResponse::bad_request("invalid_query", "keys takes 1 to 100 comma-separated keys"));
        return;
    }
    for (const auto& k : keys) {
        if (k.size() > kKeyMaxLength) {
            callback(ErrorResponse::bad_request("invalid_query", "a key is 200 characters at most"));
            return;
        }
    }
    callback(Response::ok(nlohmann::json{{"data", Security::Idempotency::seen(req, keys)}}));
}

}  // namespace Api
