-- Migration 035: a plan item may need no border crossing of the owner — a
-- domestic flight, or a flight of the family the owner is not on. The trip
-- page then does not ask for a crossing on its date.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE travel_trip_items ADD COLUMN IF NOT EXISTS no_crossing boolean NOT NULL DEFAULT false;
