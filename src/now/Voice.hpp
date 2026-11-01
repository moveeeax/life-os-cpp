/**
 * @file Voice.hpp
 * @brief Ani's voice on Now: a short-lived client secret for a spoken
 *        session with the OpenAI Realtime API, and the spoken replies of her
 *        agent's chat (OpenAI text to speech, the same voice). The browser
 *        talks to OpenAI over WebRTC with that secret; the API key never
 *        leaves the server. The models and the voice are fixed here, by
 *        config, so a secret opens only the session this server configured.
 *        (developers.openai.com/api/docs/guides/realtime-webrtc,
 *        developers.openai.com/api/docs/guides/text-to-speech)
 */

#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"
#include "utils/Config.hpp"
#include "utils/Utf8.hpp"

namespace Now::Voice {

/// OpenAI did not issue a secret (no answer, refused, or not what we need).
struct Unavailable : std::runtime_error {
    explicit Unavailable(const std::string& what) : std::runtime_error(what) {}
};

struct Settings {
    std::string api_key;
    std::string model;
    std::string voice;
    std::string tts_model;  // the text-to-speech model of the chat replies
};

/// One user mints at most this many sessions per window (each costs money
/// from the owner's key; a conversation is one session).
inline constexpr long long kMaxPerWindow = 30;
inline constexpr long kWindowSeconds = 3600;

inline constexpr const char* kClientSecretsUrl = "https://api.openai.com/v1/realtime/client_secrets";
inline constexpr const char* kSpeechUrl = "https://api.openai.com/v1/audio/speech";
/// The longest text the speech endpoint reads.
inline constexpr std::size_t kSpeechTextMax = 4096;
/// Spoken replies one user gets per window.
inline constexpr long long kMaxSpeechPerWindow = 120;
/// A line she says on Now (her answer to a tap): at most this many characters.
inline constexpr std::size_t kLineCharsMax = 200;
/// Lines one user gets spoken per window (taps come quicker than replies).
inline constexpr long long kMaxLinesPerWindow = 240;

/// Whether @p text may be spoken as a line: some text that is not only
/// spaces, no control characters, at most kLineCharsMax characters (UTF-8).
inline bool line_ok(const std::string& text) {
    std::size_t chars = 0;
    bool seen = false;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c == 0x7F) {
            return false;
        }
        if ((c & 0xC0) != 0x80) {
            ++chars;
        }
        if (c != ' ') {
            seen = true;
        }
    }
    return seen && chars <= kLineCharsMax;
}

/// What of a longer reply is said aloud: line breaks and runs of spaces
/// become one space; then whole sentences that fit in kLineCharsMax; a first
/// sentence too long on its own is cut at a word with an ellipsis. Empty
/// only for a blank reply.
inline std::string spoken_part(const std::string& text) {
    std::string flat;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        const bool space = c < 0x20 || c == ' ' || c == 0x7F;
        if (space) {
            if (!flat.empty() && flat.back() != ' ') {
                flat.push_back(' ');
            }
        } else {
            flat.push_back(ch);
        }
    }
    while (!flat.empty() && flat.back() == ' ') {
        flat.pop_back();
    }
    if (flat.empty() || line_ok(flat)) {
        return flat;
    }
    // The longest prefix of whole sentences within the limit.
    std::size_t best = 0;
    for (std::size_t i = 0; i < flat.size(); ++i) {
        const char c = flat[i];
        const bool ends =
            c == '.' || c == '!' || c == '?' || (i + 2 < flat.size() && flat.compare(i, 3, "\xE2\x80\xA6") == 0);
        if (!ends) {
            continue;
        }
        const std::size_t end = c == '.' || c == '!' || c == '?' ? i + 1 : i + 3;
        if (end < flat.size() && flat[end] != ' ') {
            continue;
        }
        if (Utils::Utf8::length(std::string_view(flat).substr(0, end)) > kLineCharsMax) {
            break;
        }
        best = end;
    }
    if (best > 0) {
        return flat.substr(0, best);
    }
    // One long sentence: up to the last space that leaves room for the ellipsis.
    std::size_t chars = 0;
    std::size_t last_space = 0;
    for (std::size_t i = 0; i < flat.size(); ++i) {
        if ((static_cast<unsigned char>(flat[i]) & 0xC0) != 0x80) {
            if (++chars > kLineCharsMax - 1) {
                break;
            }
        }
        if (flat[i] == ' ') {
            last_space = i;
        }
    }
    std::string cut = flat.substr(0, last_space > 0 ? last_space : 0);
    if (cut.empty()) {
        // No space at all: cut on a character boundary.
        std::size_t n = 0;
        std::size_t i = 0;
        for (; i < flat.size(); ++i) {
            if ((static_cast<unsigned char>(flat[i]) & 0xC0) != 0x80 && ++n > kLineCharsMax - 1) {
                break;
            }
        }
        cut = flat.substr(0, i);
    }
    while (!cut.empty() && (cut.back() == ' ' || cut.back() == ',')) {
        cut.pop_back();
    }
    return cut + "\xE2\x80\xA6";
}

/// How the replies sound (the speech model takes a direction next to the text).
inline constexpr const char* kSpeechInstructions =
    "Speak Russian like a native speaker, with Russian stress and intonation and no English accent. "
    "A young woman's voice, warm and a little playful, with a smile in it, like a close friend.";

/// The voice settings; nullopt while no API key is set (the feature is off).
inline std::optional<Settings> read_settings() {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    auto& cfg = Config::get();
    Settings s;
    s.api_key = cfg.get<std::string>("now.ani_voice.api_key", "ANI_VOICE_API_KEY", "");
    s.model = cfg.get<std::string>("now.ani_voice.model", "ANI_VOICE_MODEL", "gpt-realtime-mini");
    s.voice = cfg.get<std::string>("now.ani_voice.voice", "ANI_VOICE_VOICE", "marin");
    s.tts_model = cfg.get<std::string>("now.ani_voice.tts_model", "ANI_VOICE_TTS_MODEL", "gpt-4o-mini-tts");
    if (s.api_key.empty() || s.model.empty() || s.voice.empty()) {
        return std::nullopt;
    }
    return s;
}

struct Session {
    std::string value;
    long long expires_at = 0;
    std::string model;
    std::string voice;
};

/// Asks OpenAI for a client secret of a realtime session with the model and
/// voice of @p s. @throws Unavailable when none came back.
inline Session mint(Net::Http::Transport& transport, const Settings& s) {
    const nlohmann::json body{
        {"session", {{"type", "realtime"}, {"model", s.model}, {"audio", {{"output", {{"voice", s.voice}}}}}}}};
    Net::Http::Response r;
    try {
        r = transport.post_json(kClientSecretsUrl, body.dump(), {{"Authorization", "Bearer " + s.api_key}}, 10);
    } catch (const Net::Http::TransportError& e) {
        throw Unavailable(std::string("no answer: ") + e.what());
    }
    const auto parsed = nlohmann::json::parse(r.body, nullptr, /*allow_exceptions=*/false);
    if (r.status != 200) {
        std::string message;
        if (parsed.is_object() && parsed.contains("error") && parsed["error"].is_object() &&
            parsed["error"].value("message", nlohmann::json()).is_string()) {
            message = parsed["error"]["message"].get<std::string>().substr(0, 300);
        }
        throw Unavailable("status " + std::to_string(r.status) + (message.empty() ? "" : ": " + message));
    }
    if (!parsed.is_object() || !parsed.value("value", nlohmann::json()).is_string() ||
        !parsed.value("expires_at", nlohmann::json()).is_number_integer()) {
        throw Unavailable("unexpected answer");
    }
    return {parsed["value"].get<std::string>(), parsed["expires_at"].get<long long>(), s.model, s.voice};
}

/// @p text read aloud in the voice of @p s, as MP3 bytes (or @p format: "opus" for a Telegram voice note). A text
/// longer than kSpeechTextMax is cut on a character boundary. @throws Unavailable when no audio came back.
inline std::string speak(Net::Http::Transport& transport,
                         const Settings& s,
                         std::string text,
                         const std::string& format = "mp3") {
    if (text.size() > kSpeechTextMax) {
        std::size_t end = kSpeechTextMax;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
            --end;
        }
        text.resize(end);
    }
    const nlohmann::json body{{"model", s.tts_model},
                              {"voice", s.voice},
                              {"input", text},
                              {"instructions", kSpeechInstructions},
                              {"response_format", format}};
    Net::Http::Response r;
    try {
        r = transport.post_json(kSpeechUrl, body.dump(), {{"Authorization", "Bearer " + s.api_key}}, 30);
    } catch (const Net::Http::TransportError& e) {
        throw Unavailable(std::string("no answer: ") + e.what());
    }
    if (r.status != 200 || r.body.empty()) {
        throw Unavailable("status " + std::to_string(r.status));
    }
    return r.body;
}

}  // namespace Now::Voice
