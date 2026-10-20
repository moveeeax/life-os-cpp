-- Migration 057: a number goal can follow a series of the metrics catalog
-- (src/metrics/Catalog.hpp) instead of check-ins alone. goal_items.source holds
-- the canonical id ("fitness:weight", "money:expense:KZT"), NULL for a goal fed
-- by hand. Only a number goal has one; the controller checks the id itself.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE goal_items ADD COLUMN IF NOT EXISTS source text NULL;

ALTER TABLE goal_items DROP CONSTRAINT IF EXISTS goal_items_source_check;
ALTER TABLE goal_items ADD CONSTRAINT goal_items_source_check
    CHECK (source IS NULL OR kind = 'number');
