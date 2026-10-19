-- Migration 052: travel — a trip's purpose and what its bookings cost, the
-- last day of a stay in the plan, and two lists per trip: the people to reach
-- there (contacts) and what happened (moments).
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
-- Expand only: new nullable columns and new tables; binary 1.32 ignores them.

ALTER TABLE travel_trips ADD COLUMN IF NOT EXISTS purpose text
    CHECK (purpose IN ('leisure', 'business', 'family', 'relocation', 'other'));
-- What the bookings cost when they were made, in the currency they were paid
-- in: an amount and a currency, together or not at all. Never converted.
ALTER TABLE travel_trips ADD COLUMN IF NOT EXISTS booked_amount numeric(24,8) CHECK (booked_amount >= 0);
ALTER TABLE travel_trips ADD COLUMN IF NOT EXISTS booked_currency text CHECK (booked_currency ~ '^[A-Z]{3,10}$');
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'travel_trips_booked_pair') THEN
        ALTER TABLE travel_trips ADD CONSTRAINT travel_trips_booked_pair
            CHECK ((booked_amount IS NULL) = (booked_currency IS NULL));
    END IF;
END $$;

-- The check-out day of a stay; only a stay has one, on or after its date.
ALTER TABLE travel_trip_items ADD COLUMN IF NOT EXISTS end_date date;
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'travel_trip_items_end_date_stay') THEN
        ALTER TABLE travel_trip_items ADD CONSTRAINT travel_trip_items_end_date_stay
            CHECK (end_date IS NULL OR (kind = 'stay' AND end_date >= date));
    END IF;
END $$;

CREATE TABLE IF NOT EXISTS travel_trip_contacts (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    trip_id       uuid        NOT NULL REFERENCES travel_trips (id) ON DELETE CASCADE,
    name          text        NOT NULL CHECK (length(name) BETWEEN 1 AND 120),
    role          text        NOT NULL DEFAULT '' CHECK (length(role) <= 80),
    phone         text        NOT NULL DEFAULT '' CHECK (length(phone) <= 40),
    email         text        NOT NULL DEFAULT '' CHECK (length(email) <= 200),
    link          text        NOT NULL DEFAULT '' CHECK (length(link) <= 500),
    note          text        NOT NULL DEFAULT '' CHECK (length(note) <= 500),
    created_at    timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS travel_trip_contacts_trip ON travel_trip_contacts (trip_id, name);

CREATE TABLE IF NOT EXISTS travel_trip_moments (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id      uuid        NOT NULL,
    trip_id       uuid        NOT NULL REFERENCES travel_trips (id) ON DELETE CASCADE,
    date          date        NOT NULL,
    text          text        NOT NULL CHECK (length(text) BETWEEN 1 AND 2000),
    place         text        NOT NULL DEFAULT '' CHECK (length(place) <= 120),
    created_at    timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS travel_trip_moments_trip ON travel_trip_moments (trip_id, date);
