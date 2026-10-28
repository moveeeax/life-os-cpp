/**
 * @file Protocol.hpp
 * @brief A Model Context Protocol server core without a transport: one
 *        JSON-RPC request in, one HTTP status and body out. Tools only.
 *
 *        Serves both eras of the protocol (modelcontextprotocol.io,
 *        specification 2026-07-28, basic/versioning): a request whose
 *        params._meta carries "io.modelcontextprotocol/protocolVersion" is
 *        modern and served statelessly (server/discover, results marked
 *        resultType "complete"); one without it is legacy and starts with
 *        initialize (2025-11-25, 2025-06-18). No session is kept or minted.
 */

#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace Mcp {

/// The newest first. The first is modern, the others legacy.
inline constexpr std::array<std::string_view, 3> kVersions = {"2026-07-28", "2025-11-25", "2025-06-18"};
/// What a legacy initialize asking for an unknown version gets.
inline constexpr std::string_view kLegacyDefault = "2025-11-25";

inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kHeaderMismatch = -32020;
inline constexpr int kUnsupportedVersion = -32022;

struct Tool {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
};

/// Gets a tools/call result object ({content, isError}).
using Reply = std::function<void(nlohmann::json result)>;
/// Runs a tool; may answer later (from a timer), exactly once.
using Runner = std::function<void(const std::string& name, const nlohmann::json& args, Reply reply)>;

struct Server {
    std::string name;
    std::string version;
    std::string instructions;
    std::vector<Tool> tools;
    Runner run;
};

/// One POST: its body and the MCP headers that mirror it (empty if absent).
struct Request {
    nlohmann::json body;
    std::string protocol_version;  // MCP-Protocol-Version
    std::string method;            // Mcp-Method
    std::string name;              // Mcp-Name, as sent
};

/// The HTTP answer: no body for an accepted notification (202).
struct Outcome {
    int status = 200;
    std::optional<nlohmann::json> body;
};

void handle(const Server& server, const Request& request, std::function<void(Outcome)> done);

/// A tool result with one text block.
nlohmann::json tool_text(const std::string& text, bool is_error = false);

/// Decodes the "=?base64?...?=" header form; other values come back as is.
std::string decode_header(const std::string& value);

}  // namespace Mcp
