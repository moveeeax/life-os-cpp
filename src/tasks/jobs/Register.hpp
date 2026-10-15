/**
 * @file Register.hpp
 * @brief The tasks module's job handler (tasks_parse), registered into the
 *        dispatcher by Core::register_job_handlers() (src/core/JobHandlers.cpp).
 *        The handler itself is tasks/jobs/ParseJob.hpp; this declaration
 *        keeps the registration call free of its includes.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Tasks {

/// Registers tasks_parse and its dead-letter hook into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Tasks
