/**
 * @file JobHandlers.cpp
 * @brief Body for src/core/JobHandlers.hpp — compiled once into app_core.
 *        The one TU that sees every module's job registration.
 */

#include "core/JobHandlers.hpp"

#include "email/jobs/Register.hpp"
#include "fitness/jobs/Register.hpp"
#include "food/jobs/Register.hpp"
#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "money/jobs/Register.hpp"
#include "now/jobs/Register.hpp"
#include "tasks/jobs/Register.hpp"

namespace Core {

void register_job_handlers() {
    auto& d = Jobs::Dispatcher::get();
    Jobs::register_builtin_handlers();
    Email::register_jobs(d);
    Fitness::register_jobs(d);
    Food::register_jobs(d);
    Money::register_jobs(d);
    Now::register_jobs(d);
    Tasks::register_jobs(d);
}

}  // namespace Core
