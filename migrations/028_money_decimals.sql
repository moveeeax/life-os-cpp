-- Migration 028: money — amounts with eight decimals.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Every currency has a smallest unit (the tiyn of the tenge, the satoshi of
-- bitcoin) and an amount is a whole number of it; the API checks that
-- against the currency's decimals. A crypto wallet needs up to eight
-- (0.0057635 BTC), so every money amount widens to eight decimals and a
-- currency may have up to eight. The unit's name is kept with the currency.
-- The rate column widens too, a BTC quote per US dollar being about 0.00001.

ALTER TABLE money_transactions ALTER COLUMN amount TYPE numeric(24,8);
ALTER TABLE money_transactions ALTER COLUMN receipt_amount TYPE numeric(24,8);
ALTER TABLE money_accounts ALTER COLUMN opening_balance TYPE numeric(24,8);
ALTER TABLE money_categories ALTER COLUMN budget_max TYPE numeric(24,8);
ALTER TABLE money_transfers ALTER COLUMN amount_sent TYPE numeric(24,8);
ALTER TABLE money_transfers ALTER COLUMN amount_received TYPE numeric(24,8);
ALTER TABLE money_transfers ALTER COLUMN fee TYPE numeric(24,8);

ALTER TABLE money_fx_rates ALTER COLUMN per_usd TYPE numeric(30,14);

ALTER TABLE money_currencies DROP CONSTRAINT IF EXISTS money_currencies_decimals_check;
ALTER TABLE money_currencies DROP CONSTRAINT IF EXISTS money_currencies_decimals_range;
ALTER TABLE money_currencies ADD CONSTRAINT money_currencies_decimals_range CHECK (decimals BETWEEN 0 AND 8);

ALTER TABLE money_currencies ADD COLUMN IF NOT EXISTS minor_unit text NOT NULL DEFAULT ''
    CHECK (length(minor_unit) <= 30);
-- The seeded ten (src/money/Currencies.hpp has the full table).
UPDATE money_currencies c SET minor_unit = v.unit
FROM (VALUES ('KZT', 'tiyn'), ('RUB', 'kopeck'), ('USD', 'cent'), ('EUR', 'cent'), ('GBP', 'penny'),
             ('TRY', 'kuruş'), ('VND', 'dong'), ('CNY', 'fen'), ('THB', 'satang'), ('SGD', 'cent'))
     AS v (code, unit)
WHERE c.code = v.code AND c.minor_unit = '';
