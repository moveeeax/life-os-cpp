-- Migration 048: indexes for the category reads and the per-user health ranges.
--
-- money_transactions.category_id (ON DELETE RESTRICT) and
-- money_merchants.category_id (ON DELETE SET NULL) had no index, so deleting a
-- category scanned both tables across all owners (the foreign key checks and
-- CategoryRepository::remove), and the ledger's ?category= filter ran as a
-- filter after the (owner_id, date) scan. sleep_sessions and workouts had
-- single-column range indexes (014) while every read filters user_id plus the
-- range. The tables are small, so the indexes are built in the migration's
-- transaction (as in 043).
--
-- NOTE: no BEGIN/COMMIT — MigrationRunner wraps the file in one transaction.

CREATE INDEX IF NOT EXISTS idx_money_transactions_owner_category_date
    ON money_transactions (owner_id, category_id, date DESC);
CREATE INDEX IF NOT EXISTS idx_money_transactions_category
    ON money_transactions (category_id) WHERE category_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_money_merchants_category
    ON money_merchants (category_id) WHERE category_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_sleep_user_end ON sleep_sessions (user_id, end_at);
CREATE INDEX IF NOT EXISTS idx_workouts_user_start ON workouts (user_id, start_at);
