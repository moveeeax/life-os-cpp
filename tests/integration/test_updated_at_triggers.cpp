/**
 * @file test_updated_at_triggers.cpp
 * @brief Every table with an updated_at column carries the shared
 *        touch_updated_at() trigger (docs/CONVENTIONS.md §1; migration 042
 *        attached it where a module had set the column by hand). A new table
 *        with the column and no trigger fails here, not in a review.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "database/Database.hpp"
#include "test_helpers.hpp"

namespace {

class UpdatedAtTriggersTest : public TestHelpers::CoreBackedTest {
protected:
    std::string config_file_name() const override { return "updated_at_triggers_test_config.json"; }
};

}  // namespace

TEST_F(UpdatedAtTriggersTest, EveryTableWithUpdatedAtHasItsTouchTrigger) {
    const auto missing = Database::get().execute_read([](auto& txn) {
        std::vector<std::string> out;
        // users keeps its own users_touch_updated_at() from migration 001; the
        // name pattern is the same, so it passes the join like the others.
        for (const auto& row :
             txn.exec("SELECT c.table_name FROM information_schema.columns c "
                      "WHERE c.table_schema = 'public' AND c.column_name = 'updated_at' "
                      "  AND c.table_name NOT LIKE 'mig_test%' "
                      "  AND NOT EXISTS (SELECT 1 FROM pg_trigger t JOIN pg_class r ON r.oid = t.tgrelid "
                      "                  WHERE NOT t.tgisinternal AND r.relname = c.table_name "
                      "                    AND t.tgname = c.table_name || '_touch_updated_at') "
                      "ORDER BY 1")) {
            out.push_back(row[0].template as<std::string>());
        }
        return out;
    });
    EXPECT_TRUE(missing.empty()) << "tables with updated_at and no <table>_touch_updated_at trigger: " + [&] {
        std::string s;
        for (const auto& m : missing) {
            s += (s.empty() ? "" : ", ") + m;
        }
        return s;
    }();
}
