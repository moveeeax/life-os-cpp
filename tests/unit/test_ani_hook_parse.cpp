/**
 * @file test_ani_hook_parse.cpp
 * @brief What Ani's webhook takes (agent/Hook.hpp): the four item types with
 *        their limits, a plain-text body as an event, the sender's id and
 *        ttl, the body size, and the token cut out of logged paths.
 */

#include <string>
#include <variant>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/Hook.hpp"

namespace {

using nlohmann::json;
namespace Hook = Agent::Hook;

constexpr const char* kJson = "application/json";

Hook::Item ok(const json& body, const char* type = kJson) {
    auto r = Hook::parse(body.dump(), type);
    EXPECT_TRUE(std::holds_alternative<Hook::Item>(r))
        << (std::holds_alternative<Hook::Error>(r) ? std::get<Hook::Error>(r).code : "");
    return std::holds_alternative<Hook::Item>(r) ? std::get<Hook::Item>(r) : Hook::Item{};
}

Hook::Error bad(const std::string& body, const char* type = kJson) {
    auto r = Hook::parse(body, type);
    EXPECT_TRUE(std::holds_alternative<Hook::Error>(r)) << body.substr(0, 80);
    return std::holds_alternative<Hook::Error>(r) ? std::get<Hook::Error>(r) : Hook::Error{};
}

}  // namespace

TEST(AniHookParse, ValidItems) {
    const auto say = ok({{"type", "say"}, {"text", "Сборка прошла."}});
    EXPECT_EQ(say.kind, "say");
    EXPECT_EQ(say.payload, json({{"text", "Сборка прошла."}}));
    EXPECT_EQ(say.ttl, Hook::kTtlDefault);
    EXPECT_FALSE(say.source_id.has_value());

    const auto outfit = ok({{"type", "outfit"}, {"outfit", "p1"}});
    EXPECT_EQ(outfit.payload, json({{"outfit", "p1"}}));
    EXPECT_EQ(ok({{"type", "outfit"}, {"outfit", "bare"}}).payload, json({{"outfit", "bare"}}))
        << "bare is checked against the user's switch by the controller";

    EXPECT_EQ(ok({{"type", "act"}, {"name", "wave"}}).payload, json({{"name", "wave"}}));

    const auto event = ok({{"type", "event"},
                           {"source", "telegram"},
                           {"title", "Маша"},
                           {"text", "созвон через 10 минут?\nок?"},
                           {"id", "tg-42"},
                           {"ttl", 600},
                           {"extra", "dropped"}});
    EXPECT_EQ(event.kind, "event");
    EXPECT_EQ(event.payload,
              json({{"source", "telegram"}, {"title", "Маша"}, {"text", "созвон через 10 минут?\nок?"}}));
    EXPECT_EQ(event.source_id.value_or(""), "tg-42");
    EXPECT_EQ(event.ttl, 600);
}

TEST(AniHookParse, SayLimits) {
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", std::string(201, 'a')}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", "   "}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", "a\nb"}}).dump()).status, 400) << "a spoken line is one line";
    ok({{"type", "say"}, {"text", std::string(200, 'a')}});
}

TEST(AniHookParse, ValuesNotAllowedAre422) {
    const auto cape = bad(json({{"type", "outfit"}, {"outfit", "cape"}}).dump());
    EXPECT_EQ(cape.status, 422);
    EXPECT_EQ(cape.code, "unknown_outfit");
    const auto dance = bad(json({{"type", "act"}, {"name", "pole"}}).dump());
    EXPECT_EQ(dance.status, 422);
    EXPECT_EQ(dance.code, "unknown_act");
    for (const auto& name : Hook::kActs) {
        ok({{"type", "act"}, {"name", std::string(name)}});
    }
}

TEST(AniHookParse, EventLimits) {
    EXPECT_EQ(bad(json({{"type", "event"}, {"text", std::string(2001, 'a')}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "event"}, {"text", "x"}, {"source", std::string(61, 's')}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "event"}, {"text", "x"}, {"title", ""}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "event"}, {"text", std::string("a\x01")}}).dump()).status, 400);
    EXPECT_EQ(ok({{"type", "event"}, {"text", "x"}}).payload, json({{"text", "x"}}));
}

TEST(AniHookParse, TtlAndId) {
    EXPECT_EQ(ok({{"type", "say"}, {"text", "a"}, {"ttl", 999999}}).ttl, Hook::kTtlMax);
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", "a"}, {"ttl", 0}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", "a"}, {"ttl", "1h"}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", "a"}, {"id", ""}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"type", "say"}, {"text", "a"}, {"id", std::string(201, 'i')}}).dump()).status, 400);
    EXPECT_EQ(ok({{"type", "say"}, {"text", "a"}, {"id", 7}}).source_id.value_or(""), "7")
        << "a numeric id (a chat message id) is taken as text";
}

TEST(AniHookParse, BadBodies) {
    EXPECT_EQ(bad("not json").status, 400);
    EXPECT_EQ(bad("[1,2]").status, 400);
    EXPECT_EQ(bad(json({{"type", "dance"}}).dump()).status, 400);
    EXPECT_EQ(bad(json({{"text", "no type"}}).dump()).status, 400);
    EXPECT_EQ(bad("{}", "application/xml").status, 415);
}

TEST(AniHookParse, PlainTextIsAnEvent) {
    auto r = Hook::parse("  Маша: созвон?\n", "text/plain; charset=utf-8");
    ASSERT_TRUE(std::holds_alternative<Hook::Item>(r));
    EXPECT_EQ(std::get<Hook::Item>(r).kind, "event");
    EXPECT_EQ(std::get<Hook::Item>(r).payload, json({{"text", "Маша: созвон?"}}));
    EXPECT_EQ(bad("   ", "text/plain").status, 400);
    EXPECT_EQ(bad("\xff\xfe", "text/plain").status, 400) << "not UTF-8";
}

TEST(AniHookParse, BodyOver8KBIs413) {
    const auto e = bad(std::string(Hook::kBodyMax + 1, ' '));
    EXPECT_EQ(e.status, 413);
    EXPECT_EQ(e.code, "body_too_large");
}

TEST(AniHookParse, RedactsToken) {
    EXPECT_EQ(Hook::redact("/api/v1/ani/hook/aniwh_0123456789abcdef0123456789abcdef0123456789abcdef"),
              "/api/v1/ani/hook/aniwh_0123");
    EXPECT_EQ(Hook::redact("/api/v1/ani/hook/short"), "/api/v1/ani/hook/short");
    EXPECT_EQ(Hook::redact("/api/v1/ani/hook"), "/api/v1/ani/hook");
    EXPECT_EQ(Hook::redact("/api/v1/tasks/1"), "/api/v1/tasks/1");
}
