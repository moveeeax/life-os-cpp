/**
 * @file Turn.hpp
 * @brief One message to Ani's agent and its reply (ani_turns, migration 059).
 *        The API queues the turn (send); the worker runs it in the job
 *        ani_turn: wakes the user's agent if it sleeps and waits for it
 *        (DeferJob), then talks to the pod's gateway with the Life OS tools
 *        (agent/Gateway.hpp, agent/Tools.hpp) and stores the reply. A turn
 *        that cannot run ends failed with a short code the panel words;
 *        it never throws past the job, so a half-run turn is not repeated.
 *        Payload: {"turn": "<turn id>"}.
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
/// A turn still waiting for its agent after this long fails as a timeout.
inline constexpr int kTurnWaitSeconds = 480;

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

/// Queues @p text (already checked: 1 to kTurnTextMax characters, a known zone).
Sent send(const std::string& owner, const std::string& text, const std::string& tz);

/// The bracketed line put in front of a message: the person's local time
/// (@p local, empty when unknown), that the reply is spoken, and, when the
/// turn offers @p tools, that their data is read with them and not recalled.
std::string preface(const std::string& local, const std::string& tz, const nlohmann::json& tools);

/// The job handler (registered in agent/jobs/Register.cpp).
nlohmann::json process_turn(const nlohmann::json& payload);

/// Fails a turn whose job died (the dead-letter hook).
void fail_turn(const std::string& turn_id, const std::string& error);

}  // namespace Agent
