/**
 * @file Register.hpp
 * @brief The now module's job handler (now_media_sweep), registered into the
 *        dispatcher by Core::register_job_handlers() (src/core/JobHandlers.cpp).
 *        The handler itself is now/jobs/MediaSweep.hpp; this declaration
 *        keeps the registration call free of its includes.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Now {

/// Registers now_media_sweep into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Now
