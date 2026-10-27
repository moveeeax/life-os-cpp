/**
 * @file AniMcpController.cpp
 * @brief Body for src/api/AniMcpController.hpp.
 */

#include "api/AniMcpController.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <drogon/drogon.h>

#include <nlohmann/json.hpp>

#include "api/Guards.hpp"
#include "api/HandlerSupport.hpp"
#include "mcp/AniTools.hpp"
#include "mcp/Protocol.hpp"
#include "security/Auth.hpp"
#include "utils/Config.hpp"
#include "utils/ErrorResponse.hpp"
#include "utils/Strings.hpp"

namespace Api {

namespace {

using json = nlohmann::json;

HttpResponsePtr json_response(int status, const std::optional<json>& body) {
    auto r = HttpResponse::newHttpResponse();
    r->setStatusCode(static_cast<HttpStatusCode>(status));
    r->addHeader("Cache-Control", "no-store");
    if (body) {
        r->setContentTypeCode(CT_APPLICATION_JSON);
        r->setBody(body->dump());
    }
    return r;
}

/// "scheme://host[:port]" of a URL.
std::string origin_of(std::string url) {
    const auto scheme = url.find("://");
    if (scheme == std::string::npos) {
        return url;
    }
    const auto path = url.find('/', scheme + 3);
    if (path != std::string::npos) {
        url.resize(path);
    }
    return url;
}

/// An Origin header, when present, must be the site itself or a CORS origin it allows.
bool origin_allowed(const std::string& origin) {
    if (origin.empty()) {
        return true;
    }
    std::string base = "http://localhost:8080";
    std::string cors;
    if (Config::is_initialized()) {
        base = Config::get().get<std::string>("app.base_url", "APP_BASE_URL", base);
        cors = Config::get().get<std::string>("cors.allowed_origins", "CORS_ALLOWED_ORIGINS", "");
    }
    if (origin == origin_of(base)) {
        return true;
    }
    const auto allowed = Utils::Strings::split_csv_vec(cors);
    return std::find(allowed.begin(), allowed.end(), origin) != allowed.end();
}

}  // namespace

void AniMcpController::post(const HttpRequestPtr& req, Callback&& callback) {
    if (!origin_allowed(req->getHeader("Origin"))) {
        callback(json_response(403,
                               json{{"jsonrpc", "2.0"},
                                    {"id", nullptr},
                                    {"error", {{"code", Mcp::kInvalidRequest}, {"message", "Origin not allowed"}}}}));
        return;
    }
    API_REQUIRE_USER(req, callback, owner);
    if (const auto p = Security::Auth::principal_of(req); p && p->key_read_only) {
        callback(ErrorResponse::forbidden("key_read_only", "A read-only key cannot use Ani's MCP"));
        return;
    }
    Mcp::Request r;
    try {
        r.body = json::parse(req->body());
    } catch (const json::exception&) {
        callback(json_response(400,
                               json{{"jsonrpc", "2.0"},
                                    {"id", nullptr},
                                    {"error", {{"code", Mcp::kParseError}, {"message", "Parse error"}}}}));
        return;
    }
    r.protocol_version = req->getHeader("MCP-Protocol-Version");
    r.method = req->getHeader("Mcp-Method");
    r.name = req->getHeader("Mcp-Name");
    auto cb = std::make_shared<Callback>(std::move(callback));
    Mcp::handle(Mcp::ani_server(owner), r, [cb](Mcp::Outcome o) { (*cb)(json_response(o.status, o.body)); });
}

void AniMcpController::get(const HttpRequestPtr&, Callback&& callback) {
    auto r = json_response(405, std::nullopt);
    r->addHeader("Allow", "POST");
    callback(r);
}

}  // namespace Api
