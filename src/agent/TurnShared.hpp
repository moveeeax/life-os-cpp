/**
 * @file TurnShared.hpp
 * @brief What the turn job (Turn.cpp) and the webhook event job
 *        (HookEvents.cpp) share; nothing outside agent/ includes this.
 */

#pragma once

#include <string>

#include "repositories/agent/AgentRepository.hpp"

namespace Agent::detail {

/// Whether @p owner spent @p budget tokens this month.
bool over_budget(Repositories::Agent::AgentRepository& agents, const std::string& owner, long long budget);

/// Queues the event job again while events of the user wait for a turn.
void more_events(const std::string& owner);

/// Fails the user's turns that went stale (and their webhook events), so a
/// lost job or a dead worker never holds the chat or the webhook.
void reap(const std::string& owner);

}  // namespace Agent::detail
