/**
 * @file Register.hpp
 * @brief The agent's job handlers (ani_agent_start, ani_agent_stop,
 *        ani_agents_reconcile), registered into the dispatcher by
 *        Core::register_job_handlers() (src/core/JobHandlers.cpp). The
 *        handlers themselves are in agent/Lifecycle.hpp.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Agent {

/// Registers the three agent jobs into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Agent
