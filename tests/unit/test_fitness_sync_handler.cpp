/**
 * @file test_fitness_sync_handler.cpp
 * @brief The fitness_sync handler is registered by Core::register_job_handlers().
 *
 * Same class of protection as BuiltinHandlersAreRegistered: a missing or
 * renamed handler would silently send real jobs to the DLQ, and no other test
 * would notice.
 */

#include <gtest/gtest.h>

#include "core/JobHandlers.hpp"
#include "fitness/jobs/SyncJob.hpp"
#include "jobs/Dispatcher.hpp"

TEST(FitnessSyncHandler, IsRegisteredAmongBuiltins) {
    Core::register_job_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::FitnessSync::kJobType));
    EXPECT_STREQ(Jobs::FitnessSync::kJobType, "fitness_sync");
}
