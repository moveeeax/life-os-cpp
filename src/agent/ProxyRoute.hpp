/**
 * @file ProxyRoute.hpp
 * @brief Mounts the agent's model proxy (agent/Proxy.hpp) on the worker's own
 *        HTTP listener. The worker's port is cluster-internal (no ingress, no
 *        nginx route), and the proxy token is the only way in.
 */

#pragma once

namespace Agent {

/// Registers POST /ani/llm/v1/chat/completions with Drogon when the agent is
/// on; a no-op otherwise. Call before drogon::app().run().
void register_proxy_route();

}  // namespace Agent
