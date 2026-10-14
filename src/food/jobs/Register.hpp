/**
 * @file Register.hpp
 * @brief The food module's job handlers, registered into the dispatcher by
 *        Core::register_job_handlers() (src/core/JobHandlers.cpp). The
 *        handler itself is food/jobs/ParseJob.hpp; this declaration keeps
 *        the registration call free of its includes.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Food {

/// Registers food_parse and its dead-letter hook into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Food
