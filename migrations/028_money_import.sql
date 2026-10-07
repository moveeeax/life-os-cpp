-- Migration 028: money — what the one-off import from Notion needs.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- 1. Accounts and categories carry the id of the row they were imported from,
--    unique per owner, so a second run of the import updates instead of
--    duplicating (transactions and transfers have had external_id since 025).
-- 2. A crypto wallet holds amounts with more than four decimals (0.0057635
--    BTC): the transfer amounts widen to eight decimals and a currency may
--    show up to eight. The rate column widens too, a BTC quote per US dollar
--    being about 0.00001.

ALTER TABLE money_accounts ADD COLUMN IF NOT EXISTS external_id text CHECK (length(external_id) <= 120);
CREATE UNIQUE INDEX IF NOT EXISTS money_accounts_owner_external
    ON money_accounts (owner_id, external_id) WHERE external_id IS NOT NULL;

ALTER TABLE money_categories ADD COLUMN IF NOT EXISTS external_id text CHECK (length(external_id) <= 120);
CREATE UNIQUE INDEX IF NOT EXISTS money_categories_owner_external
    ON money_categories (owner_id, external_id) WHERE external_id IS NOT NULL;

ALTER TABLE money_transfers ALTER COLUMN amount_sent TYPE numeric(24,8);
ALTER TABLE money_transfers ALTER COLUMN amount_received TYPE numeric(24,8);
ALTER TABLE money_transfers ALTER COLUMN fee TYPE numeric(24,8);

ALTER TABLE money_fx_rates ALTER COLUMN per_usd TYPE numeric(30,14);

ALTER TABLE money_currencies DROP CONSTRAINT IF EXISTS money_currencies_decimals_check;
ALTER TABLE money_currencies DROP CONSTRAINT IF EXISTS money_currencies_decimals_range;
ALTER TABLE money_currencies ADD CONSTRAINT money_currencies_decimals_range CHECK (decimals BETWEEN 0 AND 8);
