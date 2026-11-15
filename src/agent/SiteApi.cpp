/**
 * @file SiteApi.cpp
 * @brief Bodies for src/agent/SiteApi.hpp.
 */

#include "agent/SiteApi.hpp"

#include <utility>

#include <spdlog/spdlog.h>

#include "agent/ToolArgs.hpp"
#include "agent/Tools.hpp"
#include "storage/S3Presign.hpp"

namespace Agent::SiteApi {

using json = nlohmann::json;

std::string url_of(const std::string& api_url, const std::string& path, const json& query) {
    std::string url = api_url + path;
    char sep = '?';
    for (const auto& kv : query) {
        url += sep + kv[0].get<std::string>() + "=" + Storage::S3::uri_encode(kv[1].get<std::string>(), true);
        sep = '&';
    }
    return url;
}

Net::Http::Request request_of(const std::string& api_url, const std::string& token, const json& req) {
    Net::Http::Request r;
    r.method = req.value("method", "GET");
    r.url = url_of(api_url, req.value("path", ""), req.value("query", json::array()));
    r.headers = {{"Authorization", "Bearer " + token}, {"Accept", "application/json"}};
    if (req.contains("body")) {
        r.body = req["body"].dump();
        r.content_type = "application/json";
    }
    r.timeout_seconds = kTimeoutSeconds;
    return r;
}

std::optional<json> read_data(Net::Http::Transport& transport,
                              const std::string& api_url,
                              const std::string& token,
                              const std::string& path,
                              json query) {
    const auto res = transport.send(request_of(api_url, token, json{{"path", path}, {"query", std::move(query)}}));
    const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
    if (res.status != 200 || !body.is_object() || !body.contains("data")) {
        return std::nullopt;
    }
    return std::optional<json>(std::in_place, body["data"]);
}

std::string tool_result(Net::Http::Transport& transport,
                        const std::string& api_url,
                        const std::string& token,
                        const json& req,
                        bool keep_ids,
                        const std::string& tool) {
    try {
        const auto res = transport.send(request_of(api_url, token, req));
        if (keep_ids) {
            // Unlike the read tools the ids stay: they are what she acts by.
            const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
            if (res.status == 200 && body.is_object() && body.contains("data")) {
                return body["data"].dump();
            }
        }
        return Tools::result_text(res);
    } catch (const Net::Http::TransportError& e) {
        spdlog::warn("ani agent: the tool {} got no answer: {}", tool, e.what());
        return ToolArgs::refusal("unavailable", "Life OS did not answer");
    }
}

}  // namespace Agent::SiteApi
