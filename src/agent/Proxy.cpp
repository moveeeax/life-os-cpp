/**
 * @file Proxy.cpp
 * @brief Bodies for src/agent/Proxy.hpp.
 */

#include "agent/Proxy.hpp"

#include <sstream>
#include <utility>

#include <spdlog/spdlog.h>

#include "utils/Crypto.hpp"

namespace Agent::Proxy {

namespace {

using json = nlohmann::json;

Reply error(long status, const std::string& type, const std::string& message) {
    return Reply{
        status, "application/json", json{{"error", {{"message", message}, {"type", type}, {"code", type}}}}.dump()};
}

void take_usage(const json& j, Usage& out) {
    if (!j.is_object() || !j.contains("usage") || !j["usage"].is_object()) {
        return;
    }
    const auto& u = j["usage"];
    const auto count = [&](const char* k) {
        return u.contains(k) && u[k].is_number_integer() ? u[k].get<long long>() : 0LL;
    };
    out.prompt_tokens = count("prompt_tokens");
    out.completion_tokens = count("completion_tokens");
    out.reported = true;
}

}  // namespace

Usage usage_of(const std::string& body, bool stream) {
    Usage out;
    if (!stream) {
        take_usage(json::parse(body, nullptr, /*allow_exceptions=*/false), out);
        return out;
    }
    std::istringstream lines(body);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.rfind("data:", 0) != 0) {
            continue;
        }
        std::string data = line.substr(5);
        if (!data.empty() && data.front() == ' ') {
            data.erase(0, 1);
        }
        if (data == "[DONE]") {
            continue;
        }
        take_usage(json::parse(data, nullptr, /*allow_exceptions=*/false), out);
    }
    return out;
}

json rewrite(json request, const Settings& s) {
    request["model"] = s.model;
    if (request.value("stream", false)) {
        if (!request.contains("stream_options") || !request["stream_options"].is_object()) {
            request["stream_options"] = json::object();
        }
        request["stream_options"]["include_usage"] = true;
    }
    return request;
}

Reply handle(const std::string& authorization,
             const std::string& body,
             const Settings& s,
             Net::Http::Transport& transport,
             const Hooks& hooks) {
    static const std::string kBearer = "Bearer ";
    if (authorization.rfind(kBearer, 0) != 0 || authorization.size() == kBearer.size()) {
        return error(401, "invalid_request_error", "a bearer token is required");
    }
    const auto owner = hooks.owner_of(Utils::Crypto::sha256_hex(authorization.substr(kBearer.size())));
    if (!owner) {
        return error(401, "invalid_request_error", "the token belongs to no running agent");
    }
    if (hooks.month_tokens(*owner) >= s.monthly_tokens) {
        return error(429, "insufficient_quota", "Ani's model budget for this month is used up");
    }
    auto request = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!request.is_object()) {
        return error(400, "invalid_request_error", "the body is not a JSON object");
    }
    const bool stream = request.value("stream", false);

    Net::Http::Request up;
    up.method = "POST";
    up.url = s.upstream_url + "/chat/completions";
    up.body = rewrite(std::move(request), s).dump();
    up.content_type = "application/json";
    up.headers = {{"Authorization", "Bearer " + s.upstream_key}};
    up.timeout_seconds = kUpstreamTimeoutSeconds;
    Net::Http::Response r;
    try {
        r = transport.send(up);
    } catch (const Net::Http::TransportError& e) {
        spdlog::warn("ani proxy: the upstream did not answer: {}", e.what());
        return error(502, "api_error", "the model did not answer");
    }

    Reply reply{r.status, stream && r.status == 200 ? "text/event-stream" : "application/json", std::move(r.body)};
    if (reply.status == 200) {
        hooks.record(*owner, usage_of(reply.body, stream));
    } else {
        spdlog::warn("ani proxy: the upstream answered {}", reply.status);
    }
    return reply;
}

}  // namespace Agent::Proxy
