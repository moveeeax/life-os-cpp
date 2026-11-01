/**
 * @file ProxyRoute.cpp
 * @brief Body for src/agent/ProxyRoute.hpp.
 */

#include "agent/ProxyRoute.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <string>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include "agent/Proxy.hpp"
#include "agent/Settings.hpp"
#include "net/Http.hpp"
#include "repositories/agent/AgentRepository.hpp"
#include "utils/BlockingPool.hpp"

namespace Agent {

namespace {

/// A model call takes seconds to minutes; the calls of all pods share these
/// threads, and past them the pod gets a 503 it retries.
Utils::BlockingPool& pool() {
    static auto* p = new Utils::BlockingPool(8, 32);
    return *p;
}

/// The request body a long session sends (its whole history) stays in memory.
constexpr std::size_t kMaxBodyBytes = 16 * 1024 * 1024;

drogon::HttpResponsePtr to_response(const Proxy::Reply& reply) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(static_cast<drogon::HttpStatusCode>(reply.status));
    resp->setContentTypeString(reply.content_type);
    resp->setBody(reply.body);
    return resp;
}

}  // namespace

void register_proxy_route() {
    const auto settings = read_settings();
    if (!settings || !serves_models(*settings)) {
        return;
    }
    drogon::app().setClientMaxBodySize(kMaxBodyBytes);
    drogon::app().setClientMaxMemoryBodySize(kMaxBodyBytes);
    drogon::app().registerHandler(
        Proxy::kPath,
        [s = *settings](const drogon::HttpRequestPtr& req,
                        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto respond = std::make_shared<std::function<void(const drogon::HttpResponsePtr&)>>(std::move(callback));
            const bool queued = pool().try_submit(
                [respond, s, auth = req->getHeader("Authorization"), body = std::string(req->body())] {
                    Proxy::Reply reply;
                    try {
                        Repositories::Agent::AgentRepository repo;
                        Proxy::Hooks hooks;
                        hooks.owner_of = [&](const std::string& hash) { return repo.owner_by_proxy_token(hash); };
                        hooks.month_tokens = [&](const std::string& owner) {
                            const auto u = repo.month_usage(owner);
                            return u.value("prompt_tokens", 0LL) + u.value("completion_tokens", 0LL);
                        };
                        hooks.record = [&](const std::string& owner, const Proxy::Usage& usage) {
                            repo.add_usage(owner, usage.prompt_tokens, usage.completion_tokens);
                            repo.touch(owner);
                        };
                        reply = Proxy::handle(auth, body, s, Net::Http::transport(), hooks);
                    } catch (const std::exception& e) {
                        spdlog::error("ani proxy failed: {}", e.what());
                        reply = Proxy::Reply{
                            500, "application/json", R"({"error":{"message":"proxy error","type":"api_error"}})"};
                    }
                    (*respond)(to_response(reply));
                });
            if (!queued) {
                (*respond)(to_response(Proxy::Reply{
                    503, "application/json", R"({"error":{"message":"busy, retry","type":"api_error"}})"}));
            }
        },
        {drogon::Post});
    spdlog::info("ani agent: model proxy on {} -> {} ({})", Proxy::kPath, settings->upstream_url, settings->model);
}

}  // namespace Agent
