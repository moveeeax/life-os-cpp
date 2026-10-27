/**
 * @file test_mcp_protocol.cpp
 * @brief The MCP core (src/mcp/Protocol): both eras of the protocol, the
 *        HTTP status of each answer, tool calls through the runner.
 */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "mcp/Protocol.hpp"

namespace {

using nlohmann::json;

Mcp::Server server() {
    Mcp::Server s;
    s.name = "test";
    s.version = "1.0";
    s.tools = {{"echo", "Says it back", json{{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}}}};
    s.run = [](const std::string& name, const json& args, Mcp::Reply reply) {
        reply(Mcp::tool_text(name + ":" + args.value("text", "")));
    };
    return s;
}

json meta(const std::string& v = "2026-07-28") {
    return json{{"io.modelcontextprotocol/protocolVersion", v},
                {"io.modelcontextprotocol/clientInfo", {{"name", "t"}, {"version", "1"}}},
                {"io.modelcontextprotocol/clientCapabilities", json::object()}};
}

Mcp::Outcome run(const json& body, const std::string& header_version = "", const std::string& method = "") {
    std::optional<Mcp::Outcome> out;
    Mcp::handle(server(), {body, header_version, method, ""}, [&](Mcp::Outcome o) { out = std::move(o); });
    EXPECT_TRUE(out.has_value());
    return out.value_or(Mcp::Outcome{});
}

json modern(const std::string& method, json params = json::object(), const std::string& v = "2026-07-28") {
    params["_meta"] = meta(v);
    return json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
}

}  // namespace

TEST(McpProtocol, InitializeEchoesVersion) {
    const auto o = run({{"jsonrpc", "2.0"},
                        {"id", 1},
                        {"method", "initialize"},
                        {"params", {{"protocolVersion", "2025-06-18"}, {"capabilities", json::object()}}}});
    EXPECT_EQ(o.status, 200);
    const json r = (*o.body)["result"];
    EXPECT_EQ(r["protocolVersion"], "2025-06-18");
    EXPECT_TRUE(r["capabilities"].contains("tools"));
    EXPECT_EQ(r["serverInfo"]["name"], "test");
}

TEST(McpProtocol, InitializeFallsBack) {
    const auto o =
        run({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", "2024-01-01"}}}});
    EXPECT_EQ((*o.body)["result"]["protocolVersion"], "2025-11-25");
}

TEST(McpProtocol, DiscoverListsVersions) {
    const auto o = run(modern("server/discover"), "2026-07-28");
    ASSERT_EQ(o.status, 200);
    const json r = (*o.body)["result"];
    EXPECT_EQ(r["resultType"], "complete");
    EXPECT_EQ(r["supportedVersions"], json({"2026-07-28", "2025-11-25", "2025-06-18"}));
    EXPECT_TRUE(r["capabilities"].contains("tools"));
    EXPECT_EQ(r["_meta"]["io.modelcontextprotocol/serverInfo"]["name"], "test");
}

TEST(McpProtocol, UnknownVersionRefused) {
    const auto o = run(modern("tools/list", json::object(), "1900-01-01"), "1900-01-01");
    EXPECT_EQ(o.status, 400);
    const json e = (*o.body)["error"];
    EXPECT_EQ(e["code"], -32022);
    EXPECT_EQ(e["data"]["requested"], "1900-01-01");
    EXPECT_EQ(e["data"]["supported"][0], "2026-07-28");
    EXPECT_EQ((*o.body)["id"], 1);
}

TEST(McpProtocol, HeaderMustMatchMeta) {
    const auto o = run(modern("tools/list"), "2025-11-25");
    EXPECT_EQ(o.status, 400);
    EXPECT_EQ((*o.body)["error"]["code"], -32020);
    const auto m = run(modern("tools/list"), "2026-07-28", "tools/call");
    EXPECT_EQ(m.status, 400);
    EXPECT_EQ((*m.body)["error"]["code"], -32020);
}

TEST(McpProtocol, ToolsListed) {
    for (const auto& o : {run(modern("tools/list"), "2026-07-28"),
                          run({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}}, "2025-11-25")}) {
        ASSERT_EQ(o.status, 200);
        const json tools = (*o.body)["result"]["tools"];
        ASSERT_EQ(tools.size(), 1u);
        EXPECT_EQ(tools[0]["name"], "echo");
        EXPECT_EQ(tools[0]["inputSchema"]["type"], "object");
    }
}

TEST(McpProtocol, CallRunsTool) {
    const auto o = run(modern("tools/call", {{"name", "echo"}, {"arguments", {{"text", "hi"}}}}), "2026-07-28");
    ASSERT_EQ(o.status, 200);
    const json r = (*o.body)["result"];
    EXPECT_EQ(r["content"][0]["text"], "echo:hi");
    EXPECT_EQ(r["isError"], false);
    EXPECT_EQ(r["resultType"], "complete");
}

TEST(McpProtocol, UnknownTool) {
    const auto o = run(modern("tools/call", {{"name", "nope"}}), "2026-07-28");
    EXPECT_EQ((*o.body)["error"]["code"], -32602);
}

TEST(McpProtocol, UnknownMethod) {
    const auto o = run(modern("resources/list"), "2026-07-28");
    EXPECT_EQ(o.status, 404);
    EXPECT_EQ((*o.body)["error"]["code"], -32601);
}

TEST(McpProtocol, BadRequest) {
    EXPECT_EQ((*run(json::array({1, 2})).body)["error"]["code"], -32600) << "no batches";
    EXPECT_EQ((*run(json{{"id", 1}, {"method", "ping"}}).body)["error"]["code"], -32600) << "no jsonrpc 2.0";
    EXPECT_EQ(run(json::array({1})).status, 400);
}

TEST(McpProtocol, NotificationGetsNoReply) {
    const auto o = run({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    EXPECT_EQ(o.status, 202);
    EXPECT_FALSE(o.body.has_value());
}

TEST(McpProtocol, PingAnswers) {
    const auto o = run({{"jsonrpc", "2.0"}, {"id", "p"}, {"method", "ping"}});
    EXPECT_EQ(o.status, 200);
    EXPECT_EQ((*o.body)["id"], "p");
    EXPECT_TRUE((*o.body)["result"].is_object());
}

TEST(McpProtocol, DecodesBase64Header) {
    EXPECT_EQ(Mcp::decode_header("=?base64?SGVsbG8sIOS4lueVjA==?="), "Hello, 世界");
    EXPECT_EQ(Mcp::decode_header("ani_say"), "ani_say");
}

TEST(McpProtocol, LateReplyOutlivesTheServer) {
    // A tool that answers later (ani_reply's wait): the server object is gone
    // by then, as in the controller, which builds it per request.
    std::vector<std::function<void()>> later;
    std::optional<Mcp::Outcome> out;
    {
        Mcp::Server s;
        s.name = std::string("late-server-") + std::string(40, 'x');
        s.version = "1.0";
        s.tools = {{"wait", "Answers later", json{{"type", "object"}}}};
        s.run = [&later](const std::string&, const json&, Mcp::Reply reply) {
            later.push_back([reply] { reply(Mcp::tool_text("done")); });
        };
        Mcp::handle(s, {modern("tools/call", {{"name", "wait"}}), "2026-07-28", "", ""}, [&](Mcp::Outcome o) {
            out = std::move(o);
        });
    }
    ASSERT_FALSE(out.has_value());
    ASSERT_EQ(later.size(), 1u);
    later[0]();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ((*out->body)["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["name"],
              std::string("late-server-") + std::string(40, 'x'));
}

TEST(McpProtocol, WrongTypesAreErrorsNotThrows) {
    EXPECT_NO_THROW({
        const auto a = run(json{{"jsonrpc", 2}, {"id", 1}, {"method", "ping"}});
        EXPECT_EQ((*a.body)["error"]["code"], -32600);
    });
    EXPECT_NO_THROW({
        const auto b =
            run({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}, {"params", {{"protocolVersion", 7}}}});
        EXPECT_EQ((*b.body)["result"]["protocolVersion"], "2025-11-25");
    });
    EXPECT_NO_THROW({
        const auto c = run(modern("tools/call", {{"name", 5}}), "2026-07-28");
        EXPECT_EQ((*c.body)["error"]["code"], -32602);
    });
}
