-- Migration 036: the person's Now settings (the weather city and the corner
-- layout), a note per day, and the family's travel documents with their expiry.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS now_settings (
    owner_id      uuid             PRIMARY KEY,
    -- The place the geocoder found for the typed city; all null without a city.
    city          text             CHECK (length(city) BETWEEN 1 AND 120),
    country_code  text             CHECK (country_code ~ '^[A-Z]{2}$'),
    latitude      double precision CHECK (latitude BETWEEN -90 AND 90),
    longitude     double precision CHECK (longitude BETWEEN -180 AND 180),
    timezone      text             CHECK (length(timezone) <= 64),
    -- {"tl","tr","bl","br"} → a chip key or null; null column = the default layout.
    corners       jsonb,
    updated_at    timestamptz      NOT NULL DEFAULT now(),
    CHECK ((city IS NULL) = (latitude IS NULL) AND (latitude IS NULL) = (longitude IS NULL))
);

CREATE TABLE IF NOT EXISTS day_note (
    owner_id    uuid        NOT NULL,
    date        date        NOT NULL,
    text        text        NOT NULL CHECK (length(text) BETWEEN 1 AND 2000),
    updated_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, date)
);

CREATE TABLE IF NOT EXISTS travel_documents (
    id           uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id     uuid        NOT NULL,
    holder       text        NOT NULL CHECK (length(holder) BETWEEN 1 AND 80),
    kind         text        NOT NULL CHECK (kind IN ('passport', 'visa', 'insurance', 'other')),
    number       text        NOT NULL DEFAULT '' CHECK (length(number) <= 60),
    valid_until  date        NOT NULL,
    note         text        NOT NULL DEFAULT '' CHECK (length(note) <= 500),
    created_at   timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS travel_documents_owner ON travel_documents (owner_id, valid_until);
