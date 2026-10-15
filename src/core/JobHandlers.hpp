/**
 * @file JobHandlers.hpp
 * @brief Registers every job handler this binary can run into the global
 *        Jobs::Dispatcher: the queue's own built-ins
 *        (jobs/BuiltinHandlers.hpp) and the handlers of each module
 *        (src/<module>/jobs/Register.hpp). core/ is the composition root,
 *        so it is the one place that may name every module; a handler's
 *        own module never includes core/.
 *
 * Declaration only — the body (and its module includes) lives in
 * JobHandlers.cpp, compiled once into app_core, so that the registration
 * (e.g. "is account_email actually registered?") stays unit-testable
 * (tests/unit/test_job_dispatch.cpp).
 */

#pragma once

namespace Core {

/**
 * @brief Register every handler. Call once at worker startup, before any
 *        worker thread can pick + dispatch. A new module's handlers are
 *        added here, as one call of its register_jobs().
 */
void register_job_handlers();

}  // namespace Core
