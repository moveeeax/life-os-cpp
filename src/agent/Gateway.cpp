/**
 * @file Gateway.cpp
 * @brief Body for src/agent/Gateway.hpp.
 */

#include "agent/Gateway.hpp"

#include <utility>

namespace Agent::Gateway {

namespace {

using json = nlohmann::json;

/// The arguments of a tool call; a call whose arguments do not parse gets none.
json arguments_of(const json& call) {
    const auto raw = call.value(json::json_pointer("/function/arguments"), std::string());
    auto args = json::parse(raw.empty() ? "{}" : raw, nullptr, /*allow_exceptions=*/false);
    return args.is_object() ? args : json::object();
}

}  // namespace

Outcome converse(Net::Http::Transport& transport,
                 const std::string& base_url,
                 const std::string& token,
                 const std::string& session,
                 const std::string& text,
                 const json& tools,
                 const ToolRunner& run) {
    Outcome out;
    json messages = json::array({{{"role", "user"}, {"content", text}}});
    for (int round = 0; round < kMaxRounds; ++round) {
        Net::Http::Request r;
        r.method = "POST";
        r.url = base_url + "/v1/chat/completions";
        r.content_type = "application/json";
        r.headers = {{"Authorization", "Bearer " + token}};
        r.timeout_seconds = kCallTimeoutSeconds;
        json request = {{"model", "openclaw"}, {"user", session}, {"messages", messages}};
        if (tools.is_array() && !tools.empty()) {
            request["tools"] = tools;
        }
        r.body = request.dump();
        Net::Http::Response res;
        try {
            res = transport.send(r);
        } catch (const Net::Http::TransportError& e) {
            throw Error("unavailable", std::string("the gateway did not answer: ") + e.what());
        }
        if (res.status != 200) {
            throw Error("unavailable", "the gateway answered " + std::to_string(res.status));
        }
        const auto body = json::parse(res.body, nullptr, /*allow_exceptions=*/false);
        const auto message = body.is_object() ? body.value(json::json_pointer("/choices/0/message"), json()) : json();
        if (!message.is_object()) {
            throw Error("failed", "the gateway's answer has no message");
        }
        const auto calls = message.value("tool_calls", json::array());
        if (!calls.is_array() || calls.empty()) {
            const auto content = message.value("content", json());
            if (!content.is_string() || content.get<std::string>().empty()) {
                throw Error("failed", "the agent answered without words");
            }
            out.reply = content.get<std::string>();
            return out;
        }
        messages.push_back(message);
        for (const auto& call : calls) {
            const std::string name = call.value(json::json_pointer("/function/name"), std::string());
            std::string result = run(name, arguments_of(call));
            if (result.size() > kToolResultMax) {
                // Cut on a character boundary: half a UTF-8 sequence would not serialize.
                std::size_t end = kToolResultMax;
                while (end > 0 && (static_cast<unsigned char>(result[end]) & 0xC0) == 0x80) {
                    --end;
                }
                result = result.substr(0, end) + "…";
            }
            out.tools.push_back(name);
            messages.push_back({{"role", "tool"}, {"tool_call_id", call.value("id", "")}, {"content", result}});
        }
    }
    throw Error("failed", "no reply after " + std::to_string(kMaxRounds) + " rounds of tools");
}

}  // namespace Agent::Gateway
