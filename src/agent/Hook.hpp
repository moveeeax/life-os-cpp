/**
 * @file Hook.hpp
 * @brief What Ani's personal webhook takes (api/AniHookController.hpp): one
 *        item per request, checked here without any I/O.
 *          say:    {"type":"say","text":<a spoken line, now/Voice.hpp line_ok>}
 *          outfit: {"type":"outfit","outfit":"p1"|"p2"|"lingerie"|"bare"}
 *          act:    {"type":"act","name":<one of kActs>}
 *          event:  {"type":"event","text":1-2000,"source"?:1-60,"title"?:1-60}
 *        Any item may carry "id" (the sender's, for de-duplication) and
 *        "ttl" (seconds to wait for the page). A text/plain body is an event.
 *        The payload keeps only the fields of its type.
 */

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <nlohmann/json.hpp>

namespace Agent::Hook {

/// The worker job that hands a user's thinking events to their agent.
/// Payload: {"owner": "<user id>"}.
inline constexpr const char* kEventJob = "ani_hook_event";
/// Hook calls per user and hour, and items that may wait for the page.
inline constexpr int kCallsPerHour = 120;
inline constexpr int kWindowSeconds = 3600;
inline constexpr int kPendingMax = 50;

inline constexpr std::size_t kBodyMax = 8192;
inline constexpr int kTtlDefault = 86400;
inline constexpr int kTtlMax = 604800;
inline constexpr std::size_t kEventTextMax = 2000;
inline constexpr std::size_t kLabelMax = 60;
inline constexpr std::size_t kIdMax = 200;

/// Her moves a hook may ask for: the gestures she plays on her ledge (the
/// EMOTES and the greeting of CLIPS in frontend/src/pages/now/ani/engine.ts).
inline constexpr std::array<std::string_view, 9> kActs = {
    "show", "greet", "peace", "shoot", "spin", "pose", "idle_happy", "idle_cat", "wave"};

inline constexpr std::array<std::string_view, 4> kOutfits = {"p1", "p2", "lingerie", "bare"};

struct Item {
    std::string kind;
    nlohmann::json payload;
    std::optional<std::string> source_id;
    int ttl = kTtlDefault;
};

/// An HTTP status (400, 413, 415, 422) and an error code.
struct Error {
    int status = 400;
    std::string code;
    std::string message;
};

std::variant<Item, Error> parse(std::string_view body, std::string_view content_type);

/// @p path with a webhook token cut to its first 10 characters, for logs.
std::string redact(std::string_view path);

}  // namespace Agent::Hook
