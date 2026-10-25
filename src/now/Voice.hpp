/**
 * @file Voice.hpp
 * @brief Ani's voice on Now: a short-lived client secret for a spoken
 *        session with the OpenAI Realtime API. The browser talks to OpenAI
 *        over WebRTC with that secret; the API key never leaves the server.
 *        The model and the voice are fixed here, by config, so a secret
 *        opens only the session this server configured.
 *        (developers.openai.com/api/docs/guides/realtime-webrtc)
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"
#include "utils/Config.hpp"

namespace Now::Voice {

/// OpenAI did not issue a secret (no answer, refused, or not what we need).
struct Unavailable : std::runtime_error {
    explicit Unavailable(const std::string& what) : std::runtime_error(what) {}
};

struct Settings {
    std::string api_key;
    std::string model;
    std::string voice;
};

/// One user mints at most this many sessions per window (each costs money
/// from the owner's key; a conversation is one session).
inline constexpr long long kMaxPerWindow = 30;
inline constexpr long kWindowSeconds = 3600;

inline constexpr const char* kClientSecretsUrl = "https://api.openai.com/v1/realtime/client_secrets";

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

}  // namespace Now::Voice
