/**
 * @file test_ani_facts.cpp
 * @brief What Ani keeps about the person (repositories/agent/FactRepository.hpp,
 *        agent/FactTools.hpp): at most 50, only the owner's, remember and
 *        forget run at once, and the turn's note lists them with their ids.
 */

#include <string>

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "agent/FactTools.hpp"
#include "database/Database.hpp"
#include "repositories/agent/FactRepository.hpp"
#include "repositories/agent/PrefsRepository.hpp"
#include "test_helpers.hpp"

namespace {

using nlohmann::json;
using Repositories::Agent::FactRepository;

constexpr const char* kAnna = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaafac1";
constexpr const char* kBoris = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbfac1";

class AniFacts : public TestHelpers::CoreBackedTest {
protected:
    FactRepository facts;
    std::string config_file_name() const override { return "ani_facts_test_config.json"; }
    void SetUp() override {
        TestHelpers::CoreBackedTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        Database::get().execute_write([](auto& txn) {
            txn.exec("DELETE FROM ani_facts");
            return true;
        });
    }
};

}  // namespace

TEST_F(AniFacts, FactsCapAndOwner) {
    for (int i = 0; i < FactRepository::kMax; ++i) {
        ASSERT_TRUE(facts.add(kAnna, "fact " + std::to_string(i), "page").has_value());
    }
    EXPECT_FALSE(facts.add(kAnna, "one more", "page").has_value()) << "50 at most";
    EXPECT_TRUE(facts.add(kBoris, "his own", "page").has_value()) << "per user";
    const std::string id = facts.list(kAnna)[0]["id"];
    EXPECT_FALSE(facts.update(kBoris, id, "x")) << "not his";
    EXPECT_FALSE(facts.remove(kBoris, id));
    EXPECT_TRUE(facts.update(kAnna, id, "Любит чай"));
    EXPECT_EQ(facts.list(kAnna)[0]["text"], "Любит чай");
}

TEST_F(AniFacts, RememberAndForgetRunAtOnce) {
    auto run = Agent::FactTools::runner(kAnna, [](const std::string&, const json&) { return std::string("rest"); });
    const json kept = json::parse(run("remember", json{{"fact", "Не любит звонки до 10"}}));
    ASSERT_TRUE(kept.contains("remembered"));
    EXPECT_EQ(facts.list(kAnna).size(), 1u);
    const std::string note = Agent::FactTools::facts_note(facts.list(kAnna));
    EXPECT_NE(note.find(kept["remembered"].get<std::string>() + ": Не любит звонки до 10"), std::string::npos);
    EXPECT_EQ(json::parse(run("forget", json{{"id", kept["remembered"]}}))["forgotten"], true);
    EXPECT_TRUE(facts.list(kAnna).empty());
    EXPECT_EQ(json::parse(run("remember", json{{"fact", std::string(201, 'x')}}))["error"], "invalid");
    EXPECT_EQ(run("habits", json::object()), "rest") << "other tools pass through";
    EXPECT_TRUE(Agent::FactTools::facts_note(json::array()).empty());
}

TEST_F(AniFacts, MutedKindsKept) {
    Repositories::Agent::PrefsRepository prefs;
    prefs.set_muted(kAnna, {"stretch", "brief"});
    EXPECT_EQ(prefs.get(kAnna)["muted"], json::array({"stretch", "brief"}));
    prefs.set_muted(kAnna, {});
    EXPECT_TRUE(prefs.get(kAnna)["muted"].empty());
}
