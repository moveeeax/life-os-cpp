/**
 * @file AccountEmailWorker.hpp
 * @brief Worker-side handler for "account_email" jobs.
 * @details Split from AccountEmails.hpp so HTTP controllers (which only
 *          enqueue) don't pull UserRepository through the email layer.
 *          Included by worker_main.cpp and the integration tests.
 *          Declaration only — the body lives in AccountEmailWorker.cpp
 *          (compiled once into app_core; docs/ARCHITECTURE.md §4).
 */

#pragma once

#include "email/AccountEmails.hpp"

namespace Email::AccountEmails {

/**
 * @brief Worker-side handler for "account_email" jobs. Reloads the user
 *        (fresh email/name, and the job may be older than an edit),
 *        then delivers. Render/SMTP errors propagate so the job is
 *        retried and eventually DLQ'd; a deleted user is permanent, so
 *        it acks as skipped instead of burning retries.
 */
json process_job(const json& payload);

}  // namespace Email::AccountEmails
