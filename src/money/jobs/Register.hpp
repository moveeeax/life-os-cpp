/**
 * @file Register.hpp
 * @brief The money module's job handlers (money_parse, money_advisor,
 *        money_rates), registered into the dispatcher by
 *        Core::register_job_handlers() (src/core/JobHandlers.cpp). The
 *        handlers are the other headers of money/jobs/; this declaration
 *        keeps the registration call free of their includes.
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Money {

/// Registers the three money handlers and the dead-letter hooks of the two
/// that own a row into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Money
