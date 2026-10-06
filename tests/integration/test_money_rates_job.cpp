/**
 * @file test_money_rates_job.cpp
 * @brief The money_rates job with a scripted source and a real database: a
 *        day stored for the quotes in use, a snapshot the source lacks, an
 *        outage for the queue, the backfill that skips stored days.
 */

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "database/Database.hpp"
#include "jobs/BuiltinHandlers.hpp"
#include "jobs/Dispatcher.hpp"
#include "jobs/Jobs.hpp"
#include "jobs/MoneyRatesHandler.hpp"
#include "net/Http.hpp"
#include "repositories/money/CurrencyRepository.hpp"
#include "repositories/money/FxRateRepository.hpp"
#include "test_helpers.hpp"

namespace {

using json = nlohmann::json;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaa6";

class ScriptedSource : public Net::Http::Transport {
public:
    std::vector<Net::Http::Response> replies;
    std::vector<std::string> urls;

    Net::Http::Response get(const std::string& url, const std::vector<std::pair<std::string, std::string>>&) override {
        urls.push_back(url);
        if (replies.empty()) {
            throw Net::Http::TransportError("no reply scripted");
        }
        auto r = replies.front();
        replies.erase(replies.begin());
        return r;
    }
    Net::Http::Response post_json(const std::string&,
                                  const std::string&,
                                  const std::vector<std::pair<std::string, std::string>>&,
                                  long) override {
        throw std::runtime_error("ScriptedSource: POST is not expected");
    }
};

std::string fixture() {
    std::ifstream in("tests/fixtures/currency_api_usd.json");
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

class MoneyRatesJobTest : public TestHelpers::CoreBackedTest {
protected:
    ScriptedSource source;
    Repositories::Money::FxRateRepository rates;

    std::string config_file_name() const override { return "money_rates_job_test_config.json"; }

    void config_overrides(json& cfg) override {
        cfg["money"]["enabled"] = true;
        cfg["jobs"]["enabled"] = true;
        cfg["jobs"]["result_ttl"] = 3600;
    }

    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Net::Http::install_for_testing(&source);
        Database::get().execute_write([](auto& txn) {
            txn.exec("TRUNCATE TABLE money_fx_rates, money_currencies");
            txn.exec_params(
                "INSERT INTO users (id, email, confirmed, role_id) "
                "VALUES ($1::uuid, $2, TRUE, (SELECT id FROM roles ORDER BY id LIMIT 1)) ON CONFLICT DO NOTHING",
                std::string(kAnna),
                std::string(kAnna) + "@example.test");
            return true;
        });
        Repositories::Money::CurrencyRepository().seed_defaults(kAnna);
    }

    void TearDown() override {
        Net::Http::install_for_testing(nullptr);
        TestHelpers::CoreBackedTest::TearDown();
    }
};

}  // namespace

TEST_F(MoneyRatesJobTest, StoresTheDaysQuotesForTheCurrenciesInUse) {
    source.replies.push_back({200, fixture()});
    const json result = Jobs::MoneyRates::process_job(json{{"date", "latest"}});
    EXPECT_EQ(result["status"], "done");
    EXPECT_EQ(result["date"], "2026-10-03");
    EXPECT_EQ(result["stored"], 10) << "the owner's ten; USD, EUR and KZT are among them";
    EXPECT_FALSE(result.contains("missing"));
    EXPECT_TRUE(rates.has_day("2026-10-03"));
    EXPECT_NEAR(rates.nearest("2026-10-03", "KZT")->per_usd, 448.36878322, 1e-6);
    EXPECT_FALSE(rates.nearest("2026-10-03", "JPY").has_value()) << "a quote no user keeps is not stored";
    EXPECT_EQ(source.urls[0], Money::RatesSource::url_for("latest"));

    // Running the same day again rewrites the same numbers.
    source.replies.push_back({200, fixture()});
    EXPECT_EQ(Jobs::MoneyRates::process_job(json{{"date", "2026-10-03"}})["status"], "done");
    EXPECT_EQ(source.urls[1], Money::RatesSource::url_for("2026-10-03"));
}

TEST_F(MoneyRatesJobTest, AQuoteTheSourceLacksIsReported) {
    source.replies.push_back({200, R"({"date":"2026-10-03","usd":{"kzt":450,"eur":0.9}})"});
    const json result = Jobs::MoneyRates::process_job(json{{"date", "latest"}});
    EXPECT_EQ(result["status"], "done");
    EXPECT_EQ(result["stored"], 3) << "KZT, EUR and USD (always 1)";
    EXPECT_EQ(result["missing"].size(), 8u);
}

TEST_F(MoneyRatesJobTest, MissingSnapshotFinishesAndOutagesThrow) {
    source.replies.push_back({404, "not found"});
    EXPECT_EQ(Jobs::MoneyRates::process_job(json{{"date", "2026-10-04"}})["status"], "no_snapshot");
    EXPECT_FALSE(rates.has_day("2026-10-04"));
    source.replies.push_back({503, "busy"});
    EXPECT_THROW(Jobs::MoneyRates::process_job(json{{"date", "latest"}}), std::runtime_error);
    EXPECT_THROW(Jobs::MoneyRates::process_job(json{{"date", "latest"}}), std::runtime_error) << "a transport failure";
}

TEST_F(MoneyRatesJobTest, BackfillEnqueuesOnlyTheMissingDays) {
    rates.put_day("2026-10-02", {{"KZT", 450}});
    EXPECT_EQ(Jobs::MoneyRates::enqueue_backfill("2026-10-01", "2026-10-03"), 2);
    auto first = Jobs::get().pick({"money_rates"}, 1);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->payload["date"], "2026-10-01");
    auto second = Jobs::get().pick({"money_rates"}, 1);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->payload["date"], "2026-10-03");
    Jobs::get().complete(first->id, json::object());
    Jobs::get().complete(second->id, json::object());
}

TEST(MoneyRatesHandler, IsRegisteredAmongBuiltins) {
    Jobs::register_builtin_handlers();
    EXPECT_TRUE(Jobs::Dispatcher::get().has_handler(Jobs::MoneyRates::kJobType));
    EXPECT_STREQ(Jobs::MoneyRates::kJobType, "money_rates");
}
