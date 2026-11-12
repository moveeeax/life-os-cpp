/**
 * @file test_travel_documents.cpp
 * @brief Documents against the end of a trip: a passport needs six months past
 *        the end, a visa or an insurance must cover the trip, the boundary
 *        days are valid, other documents are not checked. Without a trip, the
 *        documents running out within the warning window of today.
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

// Documents running out within @p warn_days of 2026-11-12, as id:days_left.
std::vector<std::string> soon(const std::vector<Document>& docs, int warn_days = Travel::kDocumentsWarnDays) {
    std::vector<std::string> out;
    for (const auto& e : Travel::expiring(docs, "2026-11-12", warn_days)) {
        out.push_back(e.document_id + ":" + std::to_string(e.days_left));
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

// 2026-11-12 + 180 days is 2027-05-11: that day is inside the window, the next one is not.
TEST(TravelDocumentsExpiring, ADocumentWithinTheWindowOfToday) {
    EXPECT_EQ(soon({{"p1", "Maksim", "passport", "2027-05-11"}}), std::vector<std::string>{"p1:180"});
    EXPECT_TRUE(soon({{"p1", "Maksim", "passport", "2027-05-12"}}).empty()) << "181 days is outside";
    EXPECT_EQ(soon({{"v1", "Olesia", "visa", "2026-11-12"}}), std::vector<std::string>{"v1:0"})
        << "valid through today still warns";
    EXPECT_EQ(soon({{"o1", "Olesia", "other", "2026-12-01"}}), std::vector<std::string>{"o1:19"})
        << "every kind is listed";
}

TEST(TravelDocumentsExpiring, TheSoonestFirstThenByHolder) {
    EXPECT_EQ(soon({{"i1", "Olesia", "insurance", "2027-01-01"},
                    {"p2", "Olesia", "passport", "2026-12-01"},
                    {"p1", "Maksim", "passport", "2026-12-01"}}),
              (std::vector<std::string>{"p1:19", "p2:19", "i1:50"}));
}

// A renewed passport or insurance silences the old one; the latest one keeps warning after it ran out.
TEST(TravelDocumentsExpiring, OnlyTheLatestPassportOrInsuranceOfAHolder) {
    EXPECT_TRUE(
        soon({{"old", "Maksim", "passport", "2026-12-01"}, {"new", "Maksim", "passport", "2036-12-01"}}).empty());
    EXPECT_TRUE(
        soon({{"old", "Maksim", "insurance", "2026-12-01"}, {"new", "Maksim", "insurance", "2027-12-01"}}).empty());
    EXPECT_EQ(soon({{"p1", "Maksim", "passport", "2026-11-02"}}), std::vector<std::string>{"p1:-10"})
        << "a run-out passport with nothing newer still warns";
    EXPECT_EQ(soon({{"p1", "Maksim", "passport", "2026-12-01"}, {"i1", "Maksim", "insurance", "2036-12-01"}}),
              std::vector<std::string>{"p1:19"})
        << "an insurance does not renew a passport";
    EXPECT_EQ(soon({{"m", "Maksim", "passport", "2026-12-01"}, {"o", "Olesia", "passport", "2036-12-01"}}),
              std::vector<std::string>{"m:19"})
        << "another holder's passport does not renew Maksim's";
}

// Two visas are two countries: each stands alone, and one that ran out is an old one.
TEST(TravelDocumentsExpiring, AVisaStandsAloneAndARunOutOneIsQuiet) {
    EXPECT_EQ(soon({{"v1", "Olesia", "visa", "2026-12-01"}, {"v2", "Olesia", "visa", "2028-01-01"}}),
              std::vector<std::string>{"v1:19"});
    EXPECT_TRUE(soon({{"v0", "Olesia", "visa", "2026-11-11"}, {"o0", "Olesia", "other", "2025-01-01"}}).empty());
}

TEST(TravelDocumentsExpiring, TheWindowFollowsTheSetting) {
    const std::vector<Document> docs{{"p1", "Maksim", "passport", "2026-12-01"}};
    EXPECT_TRUE(soon(docs, 18).empty());
    EXPECT_EQ(soon(docs, 19), std::vector<std::string>{"p1:19"});
    EXPECT_TRUE(soon(docs, -1).empty()) << "below zero lists nothing";
    EXPECT_TRUE(soon({{"p0", "Maksim", "passport", "2026-11-01"}}, -1).empty()) << "not even a run-out passport";
}
