/**
 * @file Hook.hpp
 * @brief What Ani's personal webhook takes (api/AniHookController.hpp): one
 *        item per request, checked here without any I/O.
 *          say:    {"type":"say","text":<a spoken line, now/Voice.hpp line_ok>}
 *          outfit: {"type":"outfit","outfit":"p1"|"p2"|"lingerie"|"bare"}
 *          act:    {"type":"act","name":<one of kActs>}
 *          event:  {"type":"event","text":1-2000,"source"?:1-60,"title"?:1-60}
 *          mood:   {"type":"mood","level":-1|0|1,"seconds"?:60-3600}
 *          remind: {"type":"remind","text":<a line>,"in_seconds":60-604800 | "at":ISO-8601}
 *          ask:    {"type":"ask","text":<a line>,"options"?:[1-4 of 1-30],"callback"?:https}
 *          focus:  {"type":"focus","action":"start","minutes":1-180} | {"action":"stop"}
 *          card:   {"type":"card","title":1-60,"text"?:0-200,"url"?:https,"seconds"?:5-120}
 *          wallpaper / sound: {"which"?:uuid} (sound also "off")
 *          hold:   {"type":"hold","minutes":0-480}
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

/// An ask waits for the page this long unless it says otherwise.
inline constexpr int kAskTtlDefault = 3600;
inline constexpr int kMoodSecondsDefault = 600;
inline constexpr int kCardSecondsDefault = 20;
inline constexpr long long kRemindMinSeconds = 60;
inline constexpr long long kRemindMaxSeconds = 604800;
inline constexpr int kHoldMaxMinutes = 480;
inline constexpr int kFocusMaxMinutes = 180;

struct Item {
    std::string kind;
    nlohmann::json payload;
    std::optional<std::string> source_id;
    int ttl = kTtlDefault;
    /// remind: in how many seconds, or at what time (ISO-8601); one of them.
    std::optional<long long> delay_seconds{};
    std::optional<std::string> at{};
    /// ask: where the answer goes (https); never in the payload.
    std::optional<std::string> callback{};
};

/// An HTTP status (400, 413, 415, 422) and an error code.
struct Error {
    int status = 400;
    std::string code;
    std::string message;
};

std::variant<Item, Error> parse(std::string_view body, std::string_view content_type);

/// Whether the agent's answer to webhook events is its "not worth telling"
/// word: SILENT in any case, with nothing around it but spaces and punctuation.
bool is_silent(std::string_view reply);

/// @p path with a webhook token cut to its first 10 characters, for logs.
std::string redact(std::string_view path);

}  // namespace Agent::Hook
