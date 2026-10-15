-- Migration 043: money_fx_rates read by quote first.
--
-- FxRateRepository::nearest and SummaryRepository filter `quote = $1 AND
-- date <= $2 ORDER BY date DESC LIMIT 1`; the primary key (date, quote) serves
-- the per-day writes of the money_rates job, not that read, which scanned
-- every day's rows. The table is small (one row per quote per day), so the
-- index is built in the migration's transaction. The primary key stays.
--
-- NOTE: no BEGIN/COMMIT — MigrationRunner wraps the file in one transaction.

CREATE INDEX IF NOT EXISTS idx_money_fx_rates_quote_date ON money_fx_rates (quote, date);
