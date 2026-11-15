/**
 * @file test_ani_mcp_schema.cpp
 * @brief Ani's MCP tools advertise the webhook's own limits (agent/Hook.hpp):
 *        every Hook type is a command tool, and each bound in a command's
 *        schema is the one Hook::parse holds an item to, so a client that
 *        trusts the schema never gets a 400 for a value it was told is fine.
 */

#include <cstddef>
#include <functional>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Actions.hpp"
#include "agent/Hook.hpp"
#include "mcp/AniTools.hpp"
#include "now/Voice.hpp"

namespace {

using json = nlohmann::json;
namespace Hook = Agent::Hook;

Mcp::Server server() {
    return Mcp::ani_server("00000000-0000-4000-8000-000000000000", [](double, std::function<void()>) {});
}

json schema_of(const Mcp::Server& s, const std::string& tool) {
    for (const auto& t : s.tools) {
        if (t.name == tool) {
            return t.input_schema;
        }
    }
    ADD_FAILURE() << "no tool " << tool;
    return json::object();
}

json prop(const Mcp::Server& s, const std::string& tool, const char* name) {
    const json schema = schema_of(s, tool);
    if (!schema.contains("properties") || !schema.at("properties").contains(name)) {
        ADD_FAILURE() << tool << " has no " << name;
        return json::object();
    }
    return schema.at("properties").at(name);
}

bool parses(const json& body) {
    return std::holds_alternative<Hook::Item>(Hook::parse(body.dump(), "application/json"));
}

json names_of(const auto& list) {
    json out = json::array();
    for (const auto n : list) {
        out.push_back(std::string(n));
    }
    return out;
}

}  // namespace

TEST(AniMcpSchema, EveryHookTypeIsACommandTool) {
    const auto s = server();
    for (const auto type : Hook::kTypes) {
        bool found = false;
        for (const auto& t : s.tools) {
            found = found || t.name == "ani_" + std::string(type);
        }
        EXPECT_TRUE(found) << type;
    }
    EXPECT_EQ(s.tools.size(), 26u);
    EXPECT_EQ(s.tools.front().name, "ani_say");
}

TEST(AniMcpSchema, CommandBoundsAreTheWebhooks) {
    const auto s = server();
    EXPECT_EQ(Hook::kLineCharsMax, Now::Voice::kLineCharsMax);
    for (const char* tool : {"ani_say", "ani_remind", "ani_ask"}) {
        EXPECT_EQ(prop(s, tool, "text")["maxLength"], Now::Voice::kLineCharsMax) << tool;
    }
    EXPECT_EQ(prop(s, "ani_outfit", "outfit")["enum"], names_of(Hook::kOutfits));
    EXPECT_EQ(prop(s, "ani_act", "name")["enum"], names_of(Hook::kActs));
    EXPECT_EQ(prop(s, "ani_event", "text")["maxLength"], Hook::kEventTextMax);
    EXPECT_EQ(prop(s, "ani_event", "source")["maxLength"], Hook::kLabelMax);
    EXPECT_EQ(prop(s, "ani_event", "title")["maxLength"], Hook::kLabelMax);
    EXPECT_EQ(prop(s, "ani_event", "id")["maxLength"], Hook::kIdMax);
    EXPECT_EQ(prop(s, "ani_event", "ttl")["maximum"], Hook::kTtlMax);
    EXPECT_EQ(prop(s, "ani_mood", "seconds")["minimum"], Hook::kMoodSecondsMin);
    EXPECT_EQ(prop(s, "ani_mood", "seconds")["maximum"], Hook::kMoodSecondsMax);
    EXPECT_EQ(prop(s, "ani_remind", "in_seconds")["minimum"], Hook::kRemindMinSeconds);
    EXPECT_EQ(prop(s, "ani_remind", "in_seconds")["maximum"], Hook::kRemindMaxSeconds);
    EXPECT_EQ(prop(s, "ani_ask", "options")["maxItems"], Hook::kAskOptionsMax);
    EXPECT_EQ(prop(s, "ani_ask", "options")["items"]["maxLength"], Hook::kAskOptionMax);
    EXPECT_EQ(prop(s, "ani_focus", "minutes")["maximum"], Hook::kFocusMaxMinutes);
    EXPECT_EQ(prop(s, "ani_card", "title")["maxLength"], Hook::kLabelMax);
    EXPECT_EQ(prop(s, "ani_card", "text")["maxLength"], Hook::kCardTextMax);
    EXPECT_EQ(prop(s, "ani_card", "seconds")["minimum"], Hook::kCardSecondsMin);
    EXPECT_EQ(prop(s, "ani_card", "seconds")["maximum"], Hook::kCardSecondsMax);
    EXPECT_EQ(prop(s, "ani_hold", "minutes")["maximum"], Hook::kHoldMaxMinutes);
}

TEST(AniMcpSchema, TheWebhookTakesWhatTheSchemaAllowsAndNoMore) {
    EXPECT_TRUE(parses({{"type", "mood"}, {"level", 0}, {"seconds", Hook::kMoodSecondsMin}}));
    EXPECT_TRUE(parses({{"type", "mood"}, {"level", 0}, {"seconds", Hook::kMoodSecondsMax}}));
    EXPECT_FALSE(parses({{"type", "mood"}, {"level", 0}, {"seconds", Hook::kMoodSecondsMin - 1}}));
    EXPECT_FALSE(parses({{"type", "mood"}, {"level", 0}, {"seconds", Hook::kMoodSecondsMax + 1}}));
    EXPECT_TRUE(parses({{"type", "card"}, {"title", "CI"}, {"seconds", Hook::kCardSecondsMin}}));
    EXPECT_TRUE(parses({{"type", "card"}, {"title", "CI"}, {"seconds", Hook::kCardSecondsMax}}));
    EXPECT_FALSE(parses({{"type", "card"}, {"title", "CI"}, {"seconds", Hook::kCardSecondsMin - 1}}));
    EXPECT_FALSE(parses({{"type", "card"}, {"title", "CI"}, {"seconds", Hook::kCardSecondsMax + 1}}));
    EXPECT_TRUE(parses({{"type", "card"}, {"title", "CI"}, {"text", std::string(Hook::kCardTextMax, 'x')}}));
    EXPECT_FALSE(parses({{"type", "card"}, {"title", "CI"}, {"text", std::string(Hook::kCardTextMax + 1, 'x')}}));
    EXPECT_TRUE(parses({{"type", "remind"}, {"text", "Встань"}, {"in_seconds", Hook::kRemindMaxSeconds}}));
    EXPECT_FALSE(parses({{"type", "remind"}, {"text", "Встань"}, {"in_seconds", Hook::kRemindMaxSeconds + 1}}));
    EXPECT_FALSE(parses({{"type", "remind"}, {"text", "Встань"}, {"in_seconds", Hook::kRemindMinSeconds - 1}}));
    EXPECT_TRUE(parses({{"type", "focus"}, {"action", "start"}, {"minutes", Hook::kFocusMaxMinutes}}));
    EXPECT_FALSE(parses({{"type", "focus"}, {"action", "start"}, {"minutes", Hook::kFocusMaxMinutes + 1}}));
    EXPECT_TRUE(parses({{"type", "hold"}, {"minutes", Hook::kHoldMaxMinutes}}));
    EXPECT_FALSE(parses({{"type", "hold"}, {"minutes", Hook::kHoldMaxMinutes + 1}}));
    EXPECT_TRUE(parses({{"type", "say"}, {"text", std::string(Hook::kLineCharsMax, 'x')}}));
    EXPECT_FALSE(parses({{"type", "say"}, {"text", std::string(Hook::kLineCharsMax + 1, 'x')}}));
    EXPECT_TRUE(parses({{"type", "event"}, {"text", std::string(Hook::kEventTextMax, 'x')}}));
    EXPECT_FALSE(parses({{"type", "event"}, {"text", std::string(Hook::kEventTextMax + 1, 'x')}}));
    json options = json::array();
    for (std::size_t i = 0; i < Hook::kAskOptionsMax; ++i) {
        options.push_back(std::string(Hook::kAskOptionMax, 'a'));
    }
    EXPECT_TRUE(parses({{"type", "ask"}, {"text", "Чай?"}, {"options", options}}));
    options.push_back("b");
    EXPECT_FALSE(parses({{"type", "ask"}, {"text", "Чай?"}, {"options", options}}));
}

TEST(AniMcpSchema, ProposalToolsTakeTheirWriteToolsSchema) {
    const auto s = server();
    EXPECT_EQ(schema_of(s, "ani_task"), Agent::Actions::schema_of("create_task"));
    EXPECT_EQ(schema_of(s, "ani_habit"), Agent::Actions::schema_of("check_habit"));
    EXPECT_EQ(schema_of(s, "ani_note"), Agent::Actions::schema_of("add_note"));
    EXPECT_EQ(schema_of(s, "ani_food"), Agent::Actions::schema_of("add_food"));
    EXPECT_EQ(schema_of(s, "ani_spend"), Agent::Actions::schema_of("add_spend"));
    EXPECT_TRUE(Agent::Actions::schema_of("habits").is_null()) << "a read tool has no write schema";
}
