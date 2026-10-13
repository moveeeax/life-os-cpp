-- Migration 034: a calendar-year window for day counters (Russian tax residency
-- takes the year's final status from the days within the calendar year).
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE travel_rules DROP CONSTRAINT IF EXISTS travel_rules_window_kind_check;
ALTER TABLE travel_rules ADD CONSTRAINT travel_rules_window_kind_check
    CHECK (window_kind IN ('rolling_days', 'rolling_12_months_in_year', 'calendar_year', 'since_last_entry'));
