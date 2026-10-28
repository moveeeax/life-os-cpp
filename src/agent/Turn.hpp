/**
 * @file Turn.hpp
 * @brief One message to Ani's agent and its reply (ani_turns, migration 059).
 *        The API queues the turn (send); the worker runs it in the job
 *        ani_turn: wakes the user's agent if it sleeps and waits for it
 *        (DeferJob), then talks to the pod's gateway with the Life OS tools
 *        (agent/Gateway.hpp, agent/Tools.hpp; the changes it proposes are
 *        agent/Actions.hpp) and stores the reply. A turn
 *        that cannot run ends failed with a short code the panel words;
 *        it never throws past the job, so a half-run turn is not repeated.
 *        Payload: {"turn": "<turn id>"}.
 *
 *        Webhook events (agent/Hook.hpp) reach the agent the same way: the
 *        job ani_hook_event gathers the user's thinking events into one
 *        webhook turn (origin hook) and queues it; its reply becomes the line
 *        the Now page plays, or nothing when the agent answers SILENT. The
 *        worker runs one turn of a user at a time; the user's own turn waits
 *        for a running webhook turn and the other way round.
 */

#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

namespace Agent {

inline constexpr const char* kTurnJob = "ani_turn";
/// Messages per user and hour.
inline constexpr int kTurnsPerHour = 60;
/// The longest message.
inline constexpr std::size_t kTurnTextMax = 4000;
/// How long the line of a reply to an MCP message waits for the Now page.
inline constexpr int kMcpLineTtl = 600;
/// A turn still waiting for its agent after this long fails as a timeout.
inline constexpr int kTurnWaitSeconds = 480;
/// A first call refused for any reason is sent again while the turn is this
/// young: the agent was most likely woken for it and is still preparing.
inline constexpr int kFirstRefusalRetrySeconds = 120;

enum class Send {
    Queued,  // the turn is in; its view is in `turn`
    Open,    // another turn of the user is still running
    Busy,    // every agent slot is taken and the user's agent sleeps
    Budget,  // the user spent this month's tokens
    Off,     // the agent is switched off or not configured
};

struct Sent {
    Send status = Send::Off;
    nlohmann::json turn;
};

/// Queues @p text (already checked: 1 to kTurnTextMax characters, a known zone)
/// as a turn of @p origin: "user" (the chat on Now) or "mcp" (her MCP).
Sent send(const std::string& owner, const std::string& text, const std::string& tz, const std::string& origin = "user");

/// Opens an MCP turn proposing a change with one of her agent's write tools
/// (@p tool: create_task, check_habit or add_note; @p args as the tool takes
/// them). The worker resolves it without the agent; the user confirms the
/// proposal under the turn on Now.
Sent propose(const std::string& owner, const std::string& tool, const nlohmann::json& args);

/// The agent's session for the user's chat: the user id, then "#<n>" once the
/// chat was cleared n times, so a cleared chat is a new conversation for it.
std::string session_of(const std::string& owner, int chat_session);

/// The bracketed line put in front of a message: the person's local time
/// (@p local, empty when unknown), that the reply is spoken; when the turn
/// offers @p tools, that their data is read with them and not recalled and
/// that changes are only proposed; and what became of the proposals of the
/// previous turn (@p outcomes, [{summary, status}]).
std::string preface(const std::string& local,
                    const std::string& tz,
                    const nlohmann::json& tools,
                    const nlohmann::json& outcomes = nlohmann::json::array());

/// @p reply without the bracketed preface, which the model now and then
/// repeats at its start; it is not for the person.
std::string without_preface(const std::string& reply);

/// The job handler (registered in agent/jobs/Register.cpp).
nlohmann::json process_turn(const nlohmann::json& payload);

/// The agent's answer to webhook events that means "do not tell the person".
inline constexpr const char* kSilent = "SILENT";

/// The line in front of a webhook turn's events: they are not from the
/// person, and how to stay silent.
std::string hook_note();

/// One event as its line in a webhook turn: "[source, title] text".
std::string event_line(const nlohmann::json& payload);

/// The ani_hook_event job: the user's thinking events join their queued
/// webhook turn or open one. Payload: {"owner": "<user id>"}.
nlohmann::json process_hook_event(const nlohmann::json& payload);

/// The dead-letter hook of ani_hook_event: the user's waiting events fail.
void fail_hook_events(const std::string& owner, const std::string& error);

/// The hourly clean-up of the webhook inbox: unplayed items past their time
/// expire, ended ones older than a week go. Payload {}.
inline constexpr const char* kInboxPurgeJob = "ani_inbox_purge";
nlohmann::json process_inbox_purge(const nlohmann::json& payload);

/// Fails a turn whose job died (the dead-letter hook).
void fail_turn(const std::string& turn_id, const std::string& error);

}  // namespace Agent
