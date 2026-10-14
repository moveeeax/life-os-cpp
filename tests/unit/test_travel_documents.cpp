/**
 * @file test_travel_documents.cpp
 * @brief Documents against the end of a trip: a passport needs six months past
 *        the end, a visa or an insurance must cover the trip, the boundary
 *        days are valid, other documents are not checked.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "travel/Documents.hpp"

using Travel::Document;

namespace {

std::vector<std::string> reasons(const std::vector<Document>& docs, const char* end) {
    std::vector<std::string> out;
    for (const auto& w : Travel::warnings(docs, end)) {
        out.push_back(w.document_id + ":" + w.reason);
    }
    return out;
}

}  // namespace

// The trip ends 2026-11-20; six months later is 2027-05-20.
TEST(TravelDocuments, APassportNeedsSixMonthsPastTheEnd) {
    EXPECT_EQ(reasons({{"p1", "passport", "2027-05-19"}}, "2026-11-20"),
              std::vector<std::string>{"p1:passport_6_months"});
    EXPECT_TRUE(reasons({{"p1", "passport", "2027-05-20"}}, "2026-11-20").empty()) << "exactly six months is enough";
    EXPECT_TRUE(reasons({{"p1", "passport", "2030-01-21"}}, "2026-11-20").empty());
}

TEST(TravelDocuments, AVisaOrInsuranceMustCoverTheTrip) {
    EXPECT_EQ(reasons({{"v1", "visa", "2026-11-19"}, {"i1", "insurance", "2026-11-01"}}, "2026-11-20"),
              (std::vector<std::string>{"v1:expires_during_trip", "i1:expires_during_trip"}));
    EXPECT_TRUE(reasons({{"v1", "visa", "2026-11-20"}}, "2026-11-20").empty()) << "valid on the last day";
}

TEST(TravelDocuments, OtherDocumentsAreNotChecked) {
    EXPECT_TRUE(reasons({{"o1", "other", "2020-01-01"}}, "2026-11-20").empty());
}

// The end of a month: Aug 31 + 6 months is Feb 28 (2027 is not a leap year).
TEST(TravelDocuments, SixMonthsFromTheEndOfAMonth) {
    EXPECT_TRUE(reasons({{"p1", "passport", "2027-02-28"}}, "2026-08-31").empty());
    EXPECT_EQ(reasons({{"p1", "passport", "2027-02-27"}}, "2026-08-31"),
              std::vector<std::string>{"p1:passport_6_months"});
}
