/**
 * @file BuiltinHandlers.hpp
 * @brief Registers the queue's own job handlers into the global
 *        Jobs::Dispatcher: the outbound webhook delivery and the echo / slow
 *        / fail demo handlers the examples and tests use. The handlers of
 *        the feature modules live next to their module
 *        (src/<module>/jobs/Register.hpp, Food::register_jobs and the like)
 *        and the email ones in src/email/jobs/; Core::register_job_handlers()
 *        (src/core/JobHandlers.hpp) calls all of them, this one included.
 *        That keeps jobs/ a leaf of the module graph: the modules depend on
 *        the queue, never the reverse (docs/ARCHITECTURE.md §4).
 *
 * Declaration only — the body (and its webhooks dependency) lives in
 * BuiltinHandlers.cpp, compiled once into app_core.
 */

#pragma once

namespace Jobs {

/**
 * @brief Register the queue's own handlers. Called by
 *        Core::register_job_handlers() at worker startup; a test that needs
 *        only the demo handlers may call it directly.
 */
void register_builtin_handlers();

}  // namespace Jobs
