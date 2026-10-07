/**
 * @file Chat.hpp
 * @brief One OpenAI-compatible chat completion, shared by the modules that
 *        ask an LLM (the food parse, the money parse, the money advisor).
 *
 * The provider is whatever the module's settings name: base URL, key, model
 * and prompt are worker configuration, nothing about a provider is in the
 * code. A 400 that names one of the optional parameters of the request
 * (max_tokens, temperature, response_format) is answered by adapting the
 * request and asking again, one parameter per round: OpenAI's reasoning
 * models want max_completion_tokens and refuse a temperature, other servers
 * do not know response_format.
 */

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "net/Http.hpp"
#include "utils/Config.hpp"
#include "utils/Utf8.hpp"

namespace Chat {

struct Settings {
    std::string base_url;  // without a trailing slash
    std::string api_key;
    std::string model;
    std::string prompt;
    long timeout_seconds = 60;
    int max_tokens = 1500;
};

/// The settings under `<key>.*` / `<ENV>_*`, or nullopt when any of base URL,
/// key, model or prompt is empty or the base URL is not http(s).
/// @param prompt_key  The key of the prompt under `key` ("prompt", "prompt_parse").
inline std::optional<Settings> read_settings(const std::string& key,
                                             const std::string& env,
                                             const std::string& prompt_key,
                                             const std::string& prompt_env,
                                             int default_max_tokens) {
    if (!Config::is_initialized()) {
        return std::nullopt;
    }
    const auto& cfg = Config::get();
    Settings s;
    s.base_url = cfg.get<std::string>(key + ".base_url", env + "_BASE_URL", "");
    s.api_key = cfg.get<std::string>(key + ".api_key", env + "_API_KEY", "");
    s.model = cfg.get<std::string>(key + ".model", env + "_MODEL", "");
    s.prompt = cfg.get<std::string>(key + "." + prompt_key, prompt_env, "");
    s.timeout_seconds = cfg.get<long>(key + ".timeout_seconds", env + "_TIMEOUT_SECONDS", 60);
    s.max_tokens = cfg.get<int>(key + ".max_tokens", env + "_MAX_TOKENS", default_max_tokens);
    while (!s.base_url.empty() && s.base_url.back() == '/') {
        s.base_url.pop_back();
    }
    const bool http = s.base_url.rfind("https://", 0) == 0 || s.base_url.rfind("http://", 0) == 0;
    if (!http || s.api_key.empty() || s.model.empty() || s.prompt.empty()) {
        return std::nullopt;
    }
    if (s.timeout_seconds < 5) {
        s.timeout_seconds = 5;
    }
    if (s.max_tokens < 100) {
        s.max_tokens = 100;
    }
    return s;
}

/// The provider did not take the request; the queue retries.
struct Retryable : std::runtime_error {
    explicit Retryable(const std::string& what) : std::runtime_error(what) {}
};

/// One chat completion. Returns the response body as JSON; a refusal or another
/// error status sets `error_code` (provider_refused, provider_error_<status>).
/// @throws Retryable on 429, 5xx, a timeout or a body that is not JSON.
inline nlohmann::json complete(Net::Http::Transport& transport,
                               const Settings& s,
                               const nlohmann::json& request,
                               std::string& error_code) {
    Net::Http::Response response;
    try {
        response = transport.post_json(s.base_url + "/chat/completions",
                                       request.dump(),
                                       {{"Authorization", "Bearer " + s.api_key}, {"Accept", "application/json"}},
                                       s.timeout_seconds);
    } catch (const Net::Http::TransportError& e) {
        throw Retryable(e.what());
    }
    if (response.status == 429 || response.status >= 500) {
        throw Retryable("provider answered HTTP " + std::to_string(response.status));
    }
    nlohmann::json body = nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
    if (response.status == 401 || response.status == 403) {
        error_code = "provider_refused";
        return nlohmann::json();
    }
    if (response.status != 200) {
        error_code = "provider_error_" + std::to_string(response.status);
        return body.is_discarded() ? nlohmann::json() : body;
    }
    if (body.is_discarded() || !body.is_object()) {
        throw Retryable("provider answered something that is not JSON");
    }
    error_code.clear();
    return body;
}

/// The provider's error text (OpenAI puts it in error.message), cut by characters.
inline std::string provider_message(const nlohmann::json& body) {
    std::string text;
    if (body.is_object() && body.contains("error")) {
        const auto& e = body["error"];
        if (e.is_object() && e.contains("message") && e["message"].is_string()) {
            text = e["message"].get<std::string>();
        } else if (e.is_string()) {
            text = e.get<std::string>();
        }
    }
    return Utils::Utf8::cut(text, 300);
}

/// Adapt the request to a 400 that names one of its optional parameters.
inline bool adapt_request(nlohmann::json& request, const nlohmann::json& body) {
    const std::string text = body.is_object() ? body.dump() : std::string();
    const auto names = [&](const char* param) { return text.find(param) != std::string::npos; };
    if (request.contains("max_tokens") && names("max_tokens")) {
        request["max_completion_tokens"] = request["max_tokens"];
        request.erase("max_tokens");
        return true;
    }
    if (request.contains("temperature") && names("temperature")) {
        request.erase("temperature");
        return true;
    }
    if (request.contains("response_format") && names("response_format")) {
        request.erase("response_format");
        return true;
    }
    return false;
}

/// complete() with up to three adapted retries on such a 400.
/// @throws Retryable.
inline nlohmann::json complete_adapting(Net::Http::Transport& transport,
                                        const Settings& s,
                                        nlohmann::json request,
                                        std::string& error_code) {
    nlohmann::json body = complete(transport, s, request, error_code);
    for (int round = 0; round < 3 && error_code == "provider_error_400" && adapt_request(request, body); ++round) {
        body = complete(transport, s, request, error_code);
    }
    return body;
}

inline std::optional<int> usage(const nlohmann::json& body, const char* key) {
    if (body.contains("usage") && body["usage"].is_object() && body["usage"].contains(key) &&
        body["usage"][key].is_number_integer()) {
        return body["usage"][key].get<int>();
    }
    return std::nullopt;
}

/// The text of the first choice, or "" when the answer has none.
inline std::string content_of(const nlohmann::json& body) {
    if (body.contains("choices") && body["choices"].is_array() && !body["choices"].empty() &&
        body["choices"][0].is_object() && body["choices"][0].contains("message") &&
        body["choices"][0]["message"].is_object() && body["choices"][0]["message"].contains("content") &&
        body["choices"][0]["message"]["content"].is_string()) {
        return body["choices"][0]["message"]["content"].get<std::string>();
    }
    return "";
}

/// The model the provider says answered, or the configured one.
inline std::string model_of(const nlohmann::json& body, const Settings& s) {
    return body.contains("model") && body["model"].is_string() ? body["model"].get<std::string>() : s.model;
}

}  // namespace Chat
