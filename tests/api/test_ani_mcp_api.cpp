/**
 * @file test_ani_mcp_api.cpp
 * @brief POST /api/v1/ani/mcp over HTTP: an ani key lists her tools; a read
 *        key is refused; a notification is 202 with no body; GET is 405; a
 *        browser page from another site is refused.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "api/AniMcpController.hpp"
#include "test_helpers.hpp"

using json = nlohmann::json;
using namespace drogon;

namespace {

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaacb";

using TestHelpers::body_of;

Security::Auth::AuthPrincipal key(const std::string& scope) {
    auto p = TestHelpers::user(kAnna);
    p.via_api_key = true;
    p.key_scope = scope;
    p.key_read_only = scope == "read";
    return p;
}

class AniMcpApi : public TestHelpers::CoreBackedTest {
protected:
    Api::AniMcpController c;

    std::string config_file_name() const override { return "ani_mcp_api_test_config.json"; }

    HttpResponsePtr post(const json& body, const std::string& scope = "ani", const std::string& origin = "") {
        auto req = TestHelpers::authed_json(key(scope), body);
        req->addHeader("MCP-Protocol-Version", "2025-11-25");
        if (!origin.empty())
            req->addHeader("Origin", origin);
        HttpResponsePtr out;
        c.post(req, [&](const HttpResponsePtr& r) { out = r; });
        return out;
    }
};

}  // namespace

TEST_F(AniMcpApi, ToolsListOverHttp) {
    const auto r = post({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->statusCode(), k200OK);
    EXPECT_EQ(r->getHeader("Cache-Control"), "no-store");
    const json tools = body_of(r)["result"]["tools"];
    ASSERT_EQ(tools.size(), 21u);
    EXPECT_EQ(tools[0]["name"], "ani_say");
}

TEST_F(AniMcpApi, ReadKeyRefused) {
    const auto r = post({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}}, "read");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->statusCode(), k403Forbidden);
}

TEST_F(AniMcpApi, NotificationIs202) {
    const auto r = post({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->statusCode(), k202Accepted);
    EXPECT_TRUE(std::string(r->body()).empty());
}

TEST_F(AniMcpApi, GetIs405) {
    HttpResponsePtr out;
    c.get(TestHelpers::authed(key("ani")), [&](const HttpResponsePtr& r) { out = r; });
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(out->statusCode(), k405MethodNotAllowed);
}

TEST_F(AniMcpApi, ForeignOriginRefused) {
    const auto r = post({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}}, "ani", "https://evil.example");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->statusCode(), k403Forbidden);
}
