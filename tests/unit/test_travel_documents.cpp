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

// Trips start 2026-11-01 unless a test says otherwise.
std::vector<std::string> reasons(const std::vector<Document>& docs, const char* end, const char* start = "2026-11-01") {
    std::vector<std::string> out;
    for (const auto& w : Travel::warnings(docs, start, end)) {
        out.push_back(w.document_id + ":" + w.reason);
    }
    return out;
}

}  // namespace

// The trip ends 2026-11-20; six months later is 2027-05-20.
TEST(TravelDocuments, APassportNeedsSixMonthsPastTheEnd) {
    EXPECT_EQ(reasons({{"p1", "Maksim", "passport", "2027-05-19"}}, "2026-11-20"),
              std::vector<std::string>{"p1:passport_6_months"});
    EXPECT_TRUE(reasons({{"p1", "Maksim", "passport", "2027-05-20"}}, "2026-11-20").empty())
        << "exactly six months is enough";
    EXPECT_TRUE(reasons({{"p1", "Maksim", "passport", "2030-01-21"}}, "2026-11-20").empty());
}

TEST(TravelDocuments, AVisaOrInsuranceMustCoverTheTrip) {
    EXPECT_EQ(
        reasons({{"v1", "Olesia", "visa", "2026-11-19"}, {"i1", "Olesia", "insurance", "2026-11-01"}}, "2026-11-20"),
        (std::vector<std::string>{"v1:expires_during_trip", "i1:expires_during_trip"}));
    EXPECT_TRUE(reasons({{"v1", "Olesia", "visa", "2026-11-20"}}, "2026-11-20").empty()) << "valid on the last day";
}

TEST(TravelDocuments, OtherDocumentsAreNotChecked) {
    EXPECT_TRUE(reasons({{"o1", "Mikhail", "other", "2020-01-01"}}, "2026-11-20").empty());
}

// The end of a month: Aug 31 + 6 months is Feb 28 (2027 is not a leap year).
TEST(TravelDocuments, SixMonthsFromTheEndOfAMonth) {
    EXPECT_TRUE(reasons({{"p1", "Maksim", "passport", "2027-02-28"}}, "2026-08-31", "2026-08-01").empty());
    EXPECT_EQ(reasons({{"p1", "Maksim", "passport", "2027-02-27"}}, "2026-08-31", "2026-08-01"),
              std::vector<std::string>{"p1:passport_6_months"});
}

// A visa or insurance that ended before the trip begins is an old document, not a warning.
TEST(TravelDocuments, AnOldVisaSaysNothingAboutALaterTrip) {
    EXPECT_TRUE(reasons({{"v0", "Olesia", "visa", "2026-04-04"}}, "2026-11-20").empty());
    EXPECT_EQ(reasons({{"v1", "Olesia", "visa", "2026-11-01"}}, "2026-11-20"),
              std::vector<std::string>{"v1:expires_during_trip"})
        << "ending on the first day still ends during the trip";
}

// Only each holder's latest passport counts: the old one kept next to the renewed one is quiet.
TEST(TravelDocuments, OnlyTheLatestPassportOfAHolder) {
    EXPECT_TRUE(reasons({{"old", "Maksim", "passport", "2026-03-11"}, {"new", "Maksim", "passport", "2032-03-11"}},
                        "2026-11-20")
                    .empty());
    EXPECT_EQ(
        reasons({{"m", "Maksim", "passport", "2026-03-11"}, {"o", "Olesia", "passport", "2032-01-01"}}, "2026-11-20"),
        std::vector<std::string>{"m:passport_6_months"})
        << "another holder's passport does not cover Maksim";
}
