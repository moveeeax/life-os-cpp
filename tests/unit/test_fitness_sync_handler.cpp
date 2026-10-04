/**
 * @file test_fitness_sync_handler.cpp
 * @brief The fitness_sync handler is registered among the built-ins.
 *
 * Same class of protection as BuiltinHandlersAreRegistered: a missing or
 * renamed handler would silently send real jobs to the DLQ, and no other test
 * would notice.
 */

#include <gtest/gtest.h>

#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/FitnessSyncHandler.hpp"

TEST(FitnessSyncHandler, IsRegisteredAmongBuiltins) {
    Jobs::register_builtin_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::FitnessSync::kJobType));
    EXPECT_STREQ(Jobs::FitnessSync::kJobType, "fitness_sync");
}
