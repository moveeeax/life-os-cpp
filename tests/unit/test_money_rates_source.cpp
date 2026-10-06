#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "money/RatesSource.hpp"
#include "net/Http.hpp"

namespace {

struct ScriptedTransport : Net::Http::Transport {
    std::vector<Net::Http::Response> replies;
    std::vector<std::string> urls;
    bool fail = false;

    Net::Http::Response get(const std::string& url, const std::vector<std::pair<std::string, std::string>>&) override {
        urls.push_back(url);
        if (fail) {
            throw Net::Http::TransportError("timed out");
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        throw std::runtime_error("not expected");
    }
};

std::string fixture() {
    std::ifstream in("tests/fixtures/currency_api_usd.json");
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

}  // namespace

TEST(MoneyRatesSource, ParsesTheUsdTableAndUpperCasesTheCodes) {
    ScriptedTransport t;
    t.replies.push_back({200, fixture()});
    const auto day = Money::RatesSource::fetch(t, "latest");
    EXPECT_EQ(day.date, "2026-10-03");
    EXPECT_NEAR(day.per_usd.at("KZT"), 448.36878322, 1e-6);
    EXPECT_NEAR(day.per_usd.at("THB"), 33.5326937, 1e-6);
    EXPECT_DOUBLE_EQ(day.per_usd.at("USD"), 1);
    EXPECT_EQ(day.per_usd.count("kzt"), 0u);
    ASSERT_EQ(t.urls.size(), 1u);
    EXPECT_EQ(t.urls[0], "https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/usd.json");
}

TEST(MoneyRatesSource, DatedUrlAndTheMissingSnapshot) {
    ScriptedTransport t;
    t.replies.push_back({404, "Couldn't find the requested file"});
    EXPECT_THROW(Money::RatesSource::fetch(t, "2026-10-04"), Money::RatesSource::NoSnapshot);
    EXPECT_EQ(t.urls[0], Money::RatesSource::url_for("2026-10-04"));
    t.replies.push_back({404, ""});
    EXPECT_THROW(Money::RatesSource::fetch(t, "latest"), Money::RatesSource::Unavailable)
        << "latest never has a missing snapshot: a 404 there is the source being down";
}

TEST(MoneyRatesSource, OutagesAndBadBodiesAreUnavailable) {
    ScriptedTransport t;
    t.replies.push_back({503, "busy"});
    EXPECT_THROW(Money::RatesSource::fetch(t, "latest"), Money::RatesSource::Unavailable);
    t.replies.push_back({200, "<html>"});
    EXPECT_THROW(Money::RatesSource::fetch(t, "latest"), Money::RatesSource::Unavailable);
    t.replies.push_back({200, R"({"date":"2026-10-03","usd":{}})"});
    EXPECT_THROW(Money::RatesSource::fetch(t, "latest"), Money::RatesSource::Unavailable);
    t.replies.push_back({200, R"({"usd":{"kzt":450}})"});
    EXPECT_THROW(Money::RatesSource::fetch(t, "latest"), Money::RatesSource::Unavailable) << "no day in the answer";
    t.fail = true;
    EXPECT_THROW(Money::RatesSource::fetch(t, "latest"), Money::RatesSource::Unavailable);
}

TEST(MoneyRatesSource, SkipsWhatIsNotACurrencyQuote) {
    ScriptedTransport t;
    t.replies.push_back({200, R"({"date":"2026-10-03","usd":{"kzt":"450","thb":33.5,"1inch":0.3,"eur":-1,"x":2}})"});
    const auto day = Money::RatesSource::fetch(t, "latest");
    EXPECT_EQ(day.per_usd.size(), 1u);
    EXPECT_DOUBLE_EQ(day.per_usd.at("THB"), 33.5);
}
