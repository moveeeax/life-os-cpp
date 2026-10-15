/**
 * @file Register.hpp
 * @brief The email job handlers (account_email, email.send), registered into
 *        the dispatcher by Core::register_job_handlers()
 *        (src/core/JobHandlers.cpp). Registering them here, not in jobs/,
 *        keeps jobs/ free of email includes: email -> jobs is the one
 *        direction of that edge (the enqueue side in AccountEmails.cpp and
 *        GenericEmail.cpp).
 */

#pragma once

namespace Jobs {
class Dispatcher;
}

namespace Email {

/// Registers account_email and email.send into @p d.
void register_jobs(Jobs::Dispatcher& d);

}  // namespace Email
