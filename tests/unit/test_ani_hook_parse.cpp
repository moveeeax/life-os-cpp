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

TEST(AniHookParse, SilentAnswers) {
    for (const char* s : {"SILENT", " SILENT\n", "Silent.", "silent", "«SILENT»", "SILENT!"}) {
        EXPECT_TRUE(Hook::is_silent(s)) << s;
    }
    for (const char* s : {"", "Silent night is on TV.", "Маша зовёт.", "SILENTLY"}) {
        EXPECT_FALSE(Hook::is_silent(s)) << s;
    }
}

TEST(AniHookParse, MoodLevelRange) {
    const auto m = ok({{"type", "mood"}, {"level", -1}});
    EXPECT_EQ(m.kind, "mood");
    EXPECT_EQ(m.payload, json({{"level", -1}, {"seconds", 600}}));
    EXPECT_EQ(ok({{"type", "mood"}, {"level", 1}, {"seconds", 60}}).payload["seconds"], 60);
    EXPECT_EQ(bad(R"({"type":"mood","level":2})").code, "invalid_level");
    EXPECT_EQ(bad(R"({"type":"mood","level":0,"seconds":30})").code, "invalid_seconds");
    EXPECT_EQ(bad(R"({"type":"mood","level":0,"seconds":3601})").code, "invalid_seconds");
}

TEST(AniHookParse, RemindNeedsOneOfAtOrIn) {
    const auto in = ok({{"type", "remind"}, {"text", "Встань"}, {"in_seconds", 1500}});
    EXPECT_EQ(in.kind, "remind");
    EXPECT_EQ(in.payload, json({{"text", "Встань"}}));
    EXPECT_EQ(in.delay_seconds, 1500);
    EXPECT_FALSE(in.at.has_value());
    const auto at = ok({{"type", "remind"}, {"text", "Созвон"}, {"at", "2026-10-28T18:30:00+05:00"}});
    EXPECT_EQ(at.at, "2026-10-28T18:30:00+05:00");
    EXPECT_EQ(bad(R"({"type":"remind","text":"x"})").code, "invalid_when");
    EXPECT_EQ(bad(R"({"type":"remind","text":"x","in_seconds":120,"at":"2026-10-28T18:30:00Z"})").code, "invalid_when");
    EXPECT_EQ(bad(R"({"type":"remind","text":"x","in_seconds":59})").code, "invalid_when");
    EXPECT_EQ(bad(R"({"type":"remind","text":"x","in_seconds":604801})").code, "invalid_when");
    EXPECT_EQ(bad(R"({"type":"remind","text":"x","at":"tomorrow"})").code, "invalid_when");
}

TEST(AniHookParse, AskOptionsLimits) {
    const auto a = ok({{"type", "ask"}, {"text", "Подтвердить?"}});
    EXPECT_EQ(a.kind, "ask");
    EXPECT_EQ(a.payload, json({{"text", "Подтвердить?"}, {"options", {"Да", "Нет"}}}));
    EXPECT_EQ(a.ttl, 3600) << "an ask waits an hour by default";
    EXPECT_EQ(ok({{"type", "ask"}, {"text", "?"}, {"ttl", 600}}).ttl, 600);
    EXPECT_EQ(ok({{"type", "ask"}, {"text", "?"}, {"options", {"A", "B", "C", "D"}}}).payload["options"].size(), 4u);
    EXPECT_EQ(bad(R"({"type":"ask","text":"?","options":["a","b","c","d","e"]})").code, "invalid_options");
    EXPECT_EQ(bad(R"({"type":"ask","text":"?","options":[]})").code, "invalid_options");
    EXPECT_EQ(bad(R"({"type":"ask","text":"?","options":["0123456789012345678901234567890"]})").code,
              "invalid_options");
}

TEST(AniHookParse, CallbackMustBeHttps) {
    const auto a = ok({{"type", "ask"}, {"text", "?"}, {"callback", "https://example.com/answer"}});
    EXPECT_EQ(a.callback, "https://example.com/answer");
    EXPECT_FALSE(a.payload.contains("callback")) << "the url is not shown to the page";
    EXPECT_EQ(bad(R"({"type":"ask","text":"?","callback":"http://example.com/a"})").code, "invalid_callback");
    EXPECT_EQ(bad(R"({"type":"ask","text":"?","callback":"ftp://x"})").code, "invalid_callback");
}

TEST(AniHookParse, FocusStartNeedsMinutes) {
    EXPECT_EQ(ok({{"type", "focus"}, {"action", "start"}, {"minutes", 25}}).payload,
              json({{"action", "start"}, {"minutes", 25}}));
    EXPECT_EQ(ok({{"type", "focus"}, {"action", "stop"}}).payload, json({{"action", "stop"}}));
    EXPECT_EQ(bad(R"({"type":"focus","action":"start"})").code, "invalid_minutes");
    EXPECT_EQ(bad(R"({"type":"focus","action":"start","minutes":181})").code, "invalid_minutes");
    EXPECT_EQ(bad(R"({"type":"focus","action":"pause"})").code, "invalid_action");
}

TEST(AniHookParse, CardLimits) {
    const auto c = ok({{"type", "card"}, {"title", "CI"}, {"text", "green"}, {"url", "https://ci.test/1"}});
    EXPECT_EQ(c.payload, json({{"title", "CI"}, {"text", "green"}, {"url", "https://ci.test/1"}, {"seconds", 20}}));
    EXPECT_EQ(ok({{"type", "card"}, {"title", "x"}, {"seconds", 5}}).payload, json({{"title", "x"}, {"seconds", 5}}));
    EXPECT_EQ(bad(R"({"type":"card"})").code, "invalid_title");
    EXPECT_EQ(bad(R"({"type":"card","title":"x","url":"http://a.test"})").code, "invalid_url");
    EXPECT_EQ(bad(R"({"type":"card","title":"x","seconds":121})").code, "invalid_seconds");
}

TEST(AniHookParse, WallpaperWhichIsUuid) {
    EXPECT_EQ(ok({{"type", "wallpaper"}}).payload, json::object());
    const std::string id = "560f0e81-7104-419e-a993-2494c52dc732";
    EXPECT_EQ(ok({{"type", "wallpaper"}, {"which", id}}).payload, json({{"which", id}}));
    EXPECT_EQ(bad(R"({"type":"wallpaper","which":"nope"})").code, "invalid_which");
}

TEST(AniHookParse, SoundOffOrUuid) {
    EXPECT_EQ(ok({{"type", "sound"}, {"which", "off"}}).payload, json({{"which", "off"}}));
    EXPECT_EQ(ok({{"type", "sound"}}).payload, json::object());
    EXPECT_EQ(bad(R"({"type":"sound","which":"loud"})").code, "invalid_which");
}

TEST(AniHookParse, HoldRange) {
    EXPECT_EQ(ok({{"type", "hold"}, {"minutes", 60}}).payload, json({{"minutes", 60}}));
    EXPECT_EQ(ok({{"type", "hold"}, {"minutes", 0}}).payload, json({{"minutes", 0}}));
    EXPECT_EQ(bad(R"({"type":"hold","minutes":481})").code, "invalid_minutes");
    EXPECT_EQ(bad(R"({"type":"hold"})").code, "invalid_minutes");
}
