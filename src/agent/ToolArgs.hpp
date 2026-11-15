/**
 * @file ToolArgs.hpp
 * @brief What every tool family of Ani's agent shares: the shape of a tool
 *        as the gateway takes it (OpenAI function tools), the reading of its
 *        arguments, and the refusal the model reads instead of an answer.
 *        Header-only; every function is inline.
 */

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "telegram/Gateway.hpp"
#include "utils/Date.hpp"

namespace Agent::ToolArgs {

/// One function tool: its name, what it does and its parameters' schema.
inline nlohmann::json tool(const char* name, const char* description, nlohmann::json parameters) {
    return nlohmann::json{
        {"type", "function"},
        {"function", {{"name", name}, {"description", description}, {"parameters", std::move(parameters)}}}};
}

/// An object schema without extra properties; "required" only when it names any.
inline nlohmann::json object(nlohmann::json properties, nlohmann::json required = nlohmann::json::array()) {
    nlohmann::json schema = {
        {"type", "object"}, {"properties", std::move(properties)}, {"additionalProperties", false}};
    if (!required.empty()) {
        schema["required"] = std::move(required);
    }
    return schema;
}

/// A string argument, or empty when it is missing or not a string. With
/// @p trim the spaces and line breaks around it go (the write tools); the
/// read tools pass it on as it came.
inline std::string text_arg(const nlohmann::json& args, const char* key, bool trim = false) {
    const auto it = args.find(key);
    if (it == args.end() || !it->is_string()) {
        return {};
    }
    std::string v = it->get<std::string>();
    if (!trim) {
        return v;
    }
    const auto first = v.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return v.substr(first, v.find_last_not_of(" \t\r\n") - first + 1);
}

/// A day argument; anything that is not a real YYYY-MM-DD day is dropped.
inline std::string day_arg(const nlohmann::json& args, const char* key, bool trim = false) {
    const std::string v = text_arg(args, key, trim);
    return Utils::Date::is_ymd(v) ? v : std::string();
}

/// A whole-number argument within [lo, hi]; nullopt when it is missing, not
/// a whole number or out of range.
inline std::optional<long long> int_arg(const nlohmann::json& args, const char* key, long long lo, long long hi) {
    const auto it = args.find(key);
    if (it == args.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    const long long n = it->get<long long>();
    return n >= lo && n <= hi ? std::optional<long long>(n) : std::nullopt;
}

/// A Telegram chat or message id argument (a string or a whole number: an
/// optional minus and 1 to 20 digits, ::Telegram::valid_id); empty when it
/// is not one.
inline std::string tg_id(const nlohmann::json& args, const char* key, bool trim = false) {
    std::string v = text_arg(args, key, trim);
    if (v.empty()) {
        if (const auto it = args.find(key); it != args.end() && it->is_number_integer()) {
            v = std::to_string(it->get<long long>());
        }
    }
    return ::Telegram::valid_id(v) ? v : std::string();
}

/// What the model reads instead of an answer: {"error", "message"} and @p extra.
inline std::string refusal(const char* error,
                           const std::string& message,
                           nlohmann::json extra = nlohmann::json::object()) {
    extra["error"] = error;
    extra["message"] = message;
    return extra.dump();
}

}  // namespace Agent::ToolArgs
