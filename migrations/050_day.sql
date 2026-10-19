-- Migration 050: the day — a mood check-in per local day, a daily target on a
-- habit ticked by hand, body measurements taken with a tape, a date on notes.
--
-- Expand-only: two new owner tables (in kOwnedTables), two nullable columns.
-- A habit without a target and a note without a date keep their behavior.
--
-- NOTE: no BEGIN/COMMIT — MigrationRunner wraps the file in one transaction.

-- One check-in per owner and local day: a score from 1 to 5, tags from the
-- fixed set the API checks, an optional note.
CREATE TABLE IF NOT EXISTS mood_checkins (
    owner_id    uuid        NOT NULL,
    date        date        NOT NULL,
    score       smallint    NOT NULL CHECK (score BETWEEN 1 AND 5),
    tags        text[]      NOT NULL DEFAULT '{}' CHECK (cardinality(tags) <= 12),
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 500),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, date)
);
DROP TRIGGER IF EXISTS mood_checkins_touch_updated_at ON mood_checkins;
CREATE TRIGGER mood_checkins_touch_updated_at
    BEFORE UPDATE ON mood_checkins
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- The amount a day of a hand-ticked habit must reach (water in ml): the day is
-- done when the tick's value, the sum of what was added that day, reaches it.
ALTER TABLE habits ADD COLUMN IF NOT EXISTS target numeric CHECK (target > 0);

-- Girths measured by hand, in cm; any of them may be left out, not all.
CREATE TABLE IF NOT EXISTS tape_measurements (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    date        date        NOT NULL,
    waist_cm    numeric(5, 1) CHECK (waist_cm > 0 AND waist_cm < 400),
    chest_cm    numeric(5, 1) CHECK (chest_cm > 0 AND chest_cm < 400),
    hips_cm     numeric(5, 1) CHECK (hips_cm > 0 AND hips_cm < 400),
    neck_cm     numeric(5, 1) CHECK (neck_cm > 0 AND neck_cm < 400),
    arm_cm      numeric(5, 1) CHECK (arm_cm > 0 AND arm_cm < 400),
    thigh_cm    numeric(5, 1) CHECK (thigh_cm > 0 AND thigh_cm < 400),
    note        text        NOT NULL DEFAULT '' CHECK (length(note) <= 500),
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now(),
    CHECK (num_nonnulls(waist_cm, chest_cm, hips_cm, neck_cm, arm_cm, thigh_cm) > 0)
);
CREATE INDEX IF NOT EXISTS tape_measurements_owner_date ON tape_measurements (owner_id, date DESC);
DROP TRIGGER IF EXISTS tape_measurements_touch_updated_at ON tape_measurements;
CREATE TRIGGER tape_measurements_touch_updated_at
    BEFORE UPDATE ON tape_measurements
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- The day a note is about; the list sorts by it when set.
ALTER TABLE notes ADD COLUMN IF NOT EXISTS date date;
CREATE INDEX IF NOT EXISTS notes_owner_day
    ON notes (owner_id, (COALESCE(date, (created_at AT TIME ZONE 'UTC')::date)) DESC, created_at DESC);
