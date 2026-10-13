-- Migration 033: travel — the person's border crossings, the rules that count
-- their days by country (tax residency, Schengen, visa limits), trips with a
-- day plan; tasks (packing) and money transactions (spends) may belong to a
-- trip.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS travel_settings (
    owner_id      uuid        PRIMARY KEY,
    home_country  text        NOT NULL CHECK (home_country ~ '^[A-Z]{2}$'),
    updated_at    timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS travel_crossings (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    date          date        NOT NULL,
    from_country  text        NOT NULL CHECK (from_country ~ '^[A-Z]{2}$'),
    to_country    text        NOT NULL CHECK (to_country ~ '^[A-Z]{2}$'),
    time          time,
    place         text        NOT NULL DEFAULT '' CHECK (length(place) <= 120),
    note          text        NOT NULL DEFAULT '' CHECK (length(note) <= 500),
    created_at    timestamptz NOT NULL DEFAULT now(),
    CHECK (from_country <> to_country)
);
CREATE INDEX IF NOT EXISTS travel_crossings_owner_date ON travel_crossings (owner_id, date, time);

CREATE TABLE IF NOT EXISTS travel_rules (
    id                  uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id            uuid        NOT NULL,
    name                text        NOT NULL CHECK (length(name) BETWEEN 1 AND 80),
    -- ISO codes, or the single group key SCHENGEN.
    countries           text[]      NOT NULL CHECK (cardinality(countries) BETWEEN 1 AND 60),
    window_kind         text        NOT NULL CHECK (window_kind IN ('rolling_days', 'rolling_12_months_in_year', 'since_last_entry')),
    window_days         integer     CHECK (window_days BETWEEN 1 AND 3650),
    threshold_days      integer     NOT NULL CHECK (threshold_days BETWEEN 1 AND 3650),
    threshold_kind      text        NOT NULL CHECK (threshold_kind IN ('at_most', 'over')),
    count_crossing_days boolean     NOT NULL DEFAULT true,
    source              text        NOT NULL DEFAULT '' CHECK (length(source) <= 600),
    position            integer     NOT NULL DEFAULT 0,
    created_at          timestamptz NOT NULL DEFAULT now(),
    CHECK ((window_kind = 'rolling_days') = (window_days IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS travel_rules_owner ON travel_rules (owner_id, position);

CREATE TABLE IF NOT EXISTS travel_trips (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    name          text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    start_date    date        NOT NULL,
    end_date      date        NOT NULL,
    countries     text[]      NOT NULL DEFAULT '{}',
    note          text        NOT NULL DEFAULT '' CHECK (length(note) <= 2000),
    created_at    timestamptz NOT NULL DEFAULT now(),
    updated_at    timestamptz NOT NULL DEFAULT now(),
    CHECK (end_date >= start_date)
);
CREATE INDEX IF NOT EXISTS travel_trips_owner_start ON travel_trips (owner_id, start_date);

CREATE TABLE IF NOT EXISTS travel_trip_items (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    trip_id       uuid        NOT NULL REFERENCES travel_trips (id) ON DELETE CASCADE,
    kind          text        NOT NULL CHECK (kind IN ('flight', 'stay', 'booking', 'note')),
    date          date        NOT NULL,
    time          time,
    title         text        NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    reference     text        NOT NULL DEFAULT '' CHECK (length(reference) <= 80),
    link          text        NOT NULL DEFAULT '' CHECK (length(link) <= 500),
    note          text        NOT NULL DEFAULT '' CHECK (length(note) <= 1000),
    position      integer     NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS travel_trip_items_trip ON travel_trip_items (trip_id, date, time);

-- Packing tasks and trip spends: a deleted trip leaves them, unlinked.
ALTER TABLE task_items ADD COLUMN IF NOT EXISTS trip_id uuid REFERENCES travel_trips (id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS task_items_trip ON task_items (trip_id) WHERE trip_id IS NOT NULL;
ALTER TABLE money_transactions ADD COLUMN IF NOT EXISTS trip_id uuid REFERENCES travel_trips (id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS money_transactions_trip ON money_transactions (trip_id) WHERE trip_id IS NOT NULL;
