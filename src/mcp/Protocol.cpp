/**
 * @file Protocol.cpp
 * @brief Body for src/mcp/Protocol.hpp.
 */

#include "mcp/Protocol.hpp"

#include <algorithm>
#include <utility>

namespace Mcp {

namespace {

using json = nlohmann::json;

constexpr const char* kVersionKey = "io.modelcontextprotocol/protocolVersion";
constexpr const char* kServerInfoKey = "io.modelcontextprotocol/serverInfo";

json versions() {
    json v = json::array();
    for (const auto s : kVersions) {
        v.push_back(std::string(s));
    }
    return v;
}

bool known(const std::string& v) {
    return std::find(kVersions.begin(), kVersions.end(), v) != kVersions.end();
}

Outcome fail(int status, const json& id, int code, const std::string& message, json data = nullptr) {
    json e{{"code", code}, {"message", message}};
    if (!data.is_null()) {
        e["data"] = std::move(data);
    }
    return {status, json{{"jsonrpc", "2.0"}, {"id", id}, {"error", e}}};
}

/// Standard base64 (RFC 4648) to bytes; stops at the first character outside the alphabet.
std::string base64_decode(std::string_view in) {
    const auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    std::string out;
    int buf = 0;
    int bits = 0;
    for (const char c : in) {
        const int v = val(c);
        if (v < 0)
            break;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

json server_info(const Server& s) {
    return json{{"name", s.name}, {"version", s.version}};
}

json tool_list(const Server& s) {
    json tools = json::array();
    for (const auto& t : s.tools) {
        tools.push_back(json{{"name", t.name}, {"description", t.description}, {"inputSchema", t.input_schema}});
    }
    return tools;
}

}  // namespace

json tool_text(const std::string& text, bool is_error) {
    return json{{"content", json::array({json{{"type", "text"}, {"text", text}}})}, {"isError", is_error}};
}

std::string decode_header(const std::string& value) {
    constexpr std::string_view head = "=?base64?";
    constexpr std::string_view tail = "?=";
    if (value.size() >= head.size() + tail.size() && value.starts_with(head) && value.ends_with(tail)) {
        return base64_decode(std::string_view(value).substr(head.size(), value.size() - head.size() - tail.size()));
    }
    return value;
}

void handle(const Server& server, const Request& request, std::function<void(Outcome)> done) {
    const json& body = request.body;
    if (!body.is_object()) {
        return done(fail(400, nullptr, kInvalidRequest, "a single JSON-RPC request object is expected"));
    }
    const json id = body.contains("id") ? body["id"] : json(nullptr);
    if (body.value("jsonrpc", "") != "2.0" || !body.contains("method") || !body["method"].is_string()) {
        return done(fail(400, id, kInvalidRequest, "not a JSON-RPC 2.0 request"));
    }
    if (!body.contains("id")) {
        // A notification: nothing to answer (notifications/initialized and the like).
        return done(Outcome{202, std::nullopt});
    }
    const std::string method = body["method"];
    const json params = body.contains("params") && body["params"].is_object() ? body["params"] : json::object();
    const json meta = params.contains("_meta") && params["_meta"].is_object() ? params["_meta"] : json::object();
    const std::string meta_version =
        meta.contains(kVersionKey) && meta[kVersionKey].is_string() ? meta[kVersionKey].get<std::string>() : "";
    const bool modern = !meta_version.empty();

    if (modern) {
        if (!request.protocol_version.empty() && request.protocol_version != meta_version) {
            return done(fail(400, id, kHeaderMismatch, "MCP-Protocol-Version does not match the request's _meta"));
        }
        if (!request.method.empty() && request.method != method) {
            return done(fail(400, id, kHeaderMismatch, "Mcp-Method does not match the request's method"));
        }
        if (meta_version != kVersions[0]) {
            return done(fail(400,
                             id,
                             kUnsupportedVersion,
                             "Unsupported protocol version",
                             json{{"supported", versions()}, {"requested", meta_version}}));
        }
    } else if (!request.protocol_version.empty() && !known(request.protocol_version)) {
        return done(fail(400,
                         id,
                         kUnsupportedVersion,
                         "Unsupported protocol version",
                         json{{"supported", versions()}, {"requested", request.protocol_version}}));
    }

    // A modern result says it is complete and who answered.
    const auto answer = [&server, modern, id, done](json result) {
        if (modern) {
            result["resultType"] = "complete";
            result["_meta"][kServerInfoKey] = server_info(server);
        }
        done(Outcome{200, json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}});
    };

    if (method == "initialize" && !modern) {
        const std::string asked = params.value("protocolVersion", "");
        const bool legacy_known = known(asked) && asked != kVersions[0];
        json r{{"protocolVersion", legacy_known ? asked : std::string(kLegacyDefault)},
               {"capabilities", {{"tools", json::object()}}},
               {"serverInfo", server_info(server)}};
        if (!server.instructions.empty()) {
            r["instructions"] = server.instructions;
        }
        return answer(std::move(r));
    }
    if (method == "server/discover") {
        json r{{"supportedVersions", versions()}, {"capabilities", {{"tools", json::object()}}}};
        if (!server.instructions.empty()) {
            r["instructions"] = server.instructions;
        }
        if (!modern) {
            r["serverInfo"] = server_info(server);
        }
        return answer(std::move(r));
    }
    if (method == "ping") {
        return answer(json::object());
    }
    if (method == "tools/list") {
        return answer(json{{"tools", tool_list(server)}});
    }
    if (method == "tools/call") {
        const std::string name = params.value("name", "");
        if (modern && !request.name.empty() && decode_header(request.name) != name) {
            return done(fail(400, id, kHeaderMismatch, "Mcp-Name does not match the tool's name"));
        }
        const bool exists =
            std::any_of(server.tools.begin(), server.tools.end(), [&](const Tool& t) { return t.name == name; });
        if (!exists) {
            return done(fail(200, id, kInvalidParams, "Unknown tool: " + name));
        }
        const json args =
            params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
        return server.run(name, args, answer);
    }
    return done(fail(modern ? 404 : 200, id, kMethodNotFound, "Method not found: " + method));
}

}  // namespace Mcp
