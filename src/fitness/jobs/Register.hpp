/**
 * @file Register.hpp
 * @brief The fitness module's job handler (fitness_sync), registered into
 *        the dispatcher by Core::register_job_handlers()
 *        (src/core/JobHandlers.cpp). The handler itself is
 *        fitness/jobs/SyncJob.hpp; this declaration keeps the registration
 *        call free of its includes.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Fitness {

/// Registers fitness_sync into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Fitness
