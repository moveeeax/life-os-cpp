/**
 * @file Register.hpp
 * @brief The private file store's job handler (files_sweep), registered into
 *        the dispatcher by Core::register_job_handlers() (src/core/JobHandlers.cpp).
 *        The handler itself is files/jobs/Sweep.hpp; this declaration keeps
 *        the registration call free of its includes.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Files {

/// Registers files_sweep into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Files
