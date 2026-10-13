/**
 * @file test_like_pattern.cpp
 * @brief Repositories::escape_like: the one quoting of LIKE's own characters
 *        the search queries share.
 */

#include <gtest/gtest.h>

#include "repositories/LikePattern.hpp"

TEST(LikePattern, QuotesPercentUnderscoreAndTheEscape) {
    EXPECT_EQ(Repositories::escape_like("50%_off\\now"), "50\\%\\_off\\\\now");
}

TEST(LikePattern, LeavesOtherTextAlone) {
    EXPECT_EQ(Repositories::escape_like(""), "");
    EXPECT_EQ(Repositories::escape_like("Жим лёжа"), "Жим лёжа");
}
