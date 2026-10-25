/**
 * @file test_now_voice.cpp
 * @brief Ani's voice sessions: the settings (off without a key), the
 *        client secret minted from OpenAI's answer, and the spoken replies.
 */

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"
#include "now/Voice.hpp"
#include "test_helpers.hpp"
#include "utils/Config.hpp"

namespace {

constexpr const char* kConfigFile = "now_voice_config.json";

struct ScriptedTransport : Net::Http::Transport {
    Net::Http::Response reply;
    bool fail = false;
    std::string url;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;

    Net::Http::Response get(const std::string&, const std::vector<std::pair<std::string, std::string>>&) override {
        throw std::runtime_error("not expected");
    }
    Net::Http::Response post_json(const std::string& u,
                                  const std::string& b,
                                  const std::vector<std::pair<std::string, std::string>>& h,
                                  long) override {
        url = u;
        body = b;
        headers = h;
        if (fail) {
            throw Net::Http::TransportError("timed out");
        }
        return reply;
    }
};

Now::Voice::Settings settings() {
    return {"sk-test", "gpt-realtime-mini", "marin", "gpt-4o-mini-tts"};
}

void load(const nlohmann::json& cfg) {
    if (Config::is_initialized()) {
        Config::shutdown();
    }
    std::ofstream(kConfigFile) << cfg.dump();
    Config::initialize(kConfigFile);
}

}  // namespace

class NowVoiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (const char* v : {"ANI_VOICE_API_KEY", "ANI_VOICE_MODEL", "ANI_VOICE_VOICE", "ANI_VOICE_TTS_MODEL"}) {
            unsetenv(v);
        }
        if (Config::is_initialized()) {
            Config::shutdown();
        }
    }

    void TearDown() override {
        if (Config::is_initialized()) {
            Config::shutdown();
        }
        std::filesystem::remove(kConfigFile);
        TestHelpers::reset_all_globals();
    }
};

TEST_F(NowVoiceTest, OffWithoutAKey) {
    EXPECT_FALSE(Now::Voice::read_settings().has_value()) << "no config at all";
    load(nlohmann::json{{"now", {{"ani_voice", {{"api_key", ""}}}}}});
    EXPECT_FALSE(Now::Voice::read_settings().has_value()) << "an empty key";
}

TEST_F(NowVoiceTest, TheKeyWithTheDefaultModelAndVoice) {
    load(nlohmann::json{{"now", {{"ani_voice", {{"api_key", "sk-test"}}}}}});
    const auto s = Now::Voice::read_settings();
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->api_key, "sk-test");
    EXPECT_EQ(s->model, "gpt-realtime-mini");
    EXPECT_EQ(s->voice, "marin");
}

TEST_F(NowVoiceTest, MintsASecretForTheConfiguredModelAndVoice) {
    ScriptedTransport t;
    t.reply = {200, R"({"value":"ek_abc","expires_at":1792908773,"session":{"type":"realtime"}})"};
    const auto session = Now::Voice::mint(t, settings());
    EXPECT_EQ(session.value, "ek_abc");
    EXPECT_EQ(session.expires_at, 1792908773);
    EXPECT_EQ(session.model, "gpt-realtime-mini");
    EXPECT_EQ(session.voice, "marin");

    EXPECT_EQ(t.url, "https://api.openai.com/v1/realtime/client_secrets");
    ASSERT_EQ(t.headers.size(), 1U);
    EXPECT_EQ(t.headers[0].first, "Authorization");
    EXPECT_EQ(t.headers[0].second, "Bearer sk-test");
    const auto sent = nlohmann::json::parse(t.body);
    EXPECT_EQ(sent["session"]["type"], "realtime");
    EXPECT_EQ(sent["session"]["model"], "gpt-realtime-mini");
    EXPECT_EQ(sent["session"]["audio"]["output"]["voice"], "marin");
}

TEST_F(NowVoiceTest, NoSecretIsUnavailable) {
    ScriptedTransport t;
    t.fail = true;
    EXPECT_THROW(Now::Voice::mint(t, settings()), Now::Voice::Unavailable) << "no answer";

    t.fail = false;
    t.reply = {401, R"({"error":{"message":"Incorrect API key provided"}})"};
    try {
        Now::Voice::mint(t, settings());
        FAIL() << "a refusal mints nothing";
    } catch (const Now::Voice::Unavailable& e) {
        EXPECT_EQ(std::string(e.what()), "status 401: Incorrect API key provided");
    }

    t.reply = {200, R"({"value":"ek_abc"})"};
    EXPECT_THROW(Now::Voice::mint(t, settings()), Now::Voice::Unavailable) << "no expiry";
    t.reply = {200, "not json"};
    EXPECT_THROW(Now::Voice::mint(t, settings()), Now::Voice::Unavailable) << "not JSON";
}

TEST_F(NowVoiceTest, SpeaksAReplyInTheConfiguredVoice) {
    ScriptedTransport t;
    t.reply = {200, std::string("ID3\x04\x00mp3", 8)};
    const auto mp3 = Now::Voice::speak(t, settings(), "Привет!");
    EXPECT_EQ(mp3, std::string("ID3\x04\x00mp3", 8));
    EXPECT_EQ(t.url, Now::Voice::kSpeechUrl);
    const auto body = nlohmann::json::parse(t.body);
    EXPECT_EQ(body["model"], "gpt-4o-mini-tts");
    EXPECT_EQ(body["voice"], "marin");
    EXPECT_EQ(body["input"], "Привет!");
    EXPECT_EQ(body["response_format"], "mp3");
    EXPECT_FALSE(body["instructions"].get<std::string>().empty());
    EXPECT_EQ(t.headers[0].second, "Bearer sk-test");
}

TEST_F(NowVoiceTest, ALongReplyIsCutOnACharacterBoundary) {
    ScriptedTransport t;
    t.reply = {200, "mp3"};
    std::string text = "x";
    while (text.size() < Now::Voice::kSpeechTextMax + 50) {
        text += "ж";
    }
    Now::Voice::speak(t, settings(), text);
    const auto input = nlohmann::json::parse(t.body)["input"].get<std::string>();
    EXPECT_LE(input.size(), Now::Voice::kSpeechTextMax);
    EXPECT_GE(input.size(), Now::Voice::kSpeechTextMax - 1);
}

TEST_F(NowVoiceTest, NoAudioIsUnavailable) {
    ScriptedTransport t;
    t.reply = {429, R"({"error":{"message":"rate limited"}})"};
    EXPECT_THROW(Now::Voice::speak(t, settings(), "Привет"), Now::Voice::Unavailable);
    t.reply = {200, ""};
    EXPECT_THROW(Now::Voice::speak(t, settings(), "Привет"), Now::Voice::Unavailable);
    t.fail = true;
    EXPECT_THROW(Now::Voice::speak(t, settings(), "Привет"), Now::Voice::Unavailable);
}
