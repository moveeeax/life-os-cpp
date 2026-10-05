-- Migration 017: workout_library — exercises and routines of the workout module.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- owner_id is the app user (users.id) but carries no foreign key: the system
-- has one owner, and library exercises (owner_id NULL) must survive the
-- TRUNCATE users CASCADE the test suite runs between cases.

CREATE TABLE IF NOT EXISTS exercises (
    id                text        PRIMARY KEY,
    source            text        NOT NULL DEFAULT 'library' CHECK (source IN ('library', 'custom')),
    owner_id          uuid,
    name              text        NOT NULL,
    category          text        NOT NULL DEFAULT 'strength',
    equipment         text,
    level             text,
    force             text,
    mechanic          text,
    primary_muscles   text[]      NOT NULL DEFAULT '{}',
    secondary_muscles text[]      NOT NULL DEFAULT '{}',
    instructions      text[]      NOT NULL DEFAULT '{}',
    images            text[]      NOT NULL DEFAULT '{}',
    -- What a set of this exercise records.
    tracking_mode     text        NOT NULL DEFAULT 'weight_reps'
        CHECK (tracking_mode IN ('weight_reps', 'bodyweight_reps', 'duration', 'distance_duration')),
    archived          boolean     NOT NULL DEFAULT false,
    created_at        timestamptz NOT NULL DEFAULT now(),
    updated_at        timestamptz NOT NULL DEFAULT now(),
    CHECK ((source = 'custom') = (owner_id IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS idx_exercises_owner ON exercises (owner_id) WHERE owner_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_exercises_name ON exercises (lower(name));

DROP TRIGGER IF EXISTS exercises_touch_updated_at ON exercises;
CREATE TRIGGER exercises_touch_updated_at
    BEFORE UPDATE ON exercises
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE IF NOT EXISTS routines (
    id         uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id   uuid        NOT NULL,
    name       text        NOT NULL,
    -- 1 = Monday … 7 = Sunday; NULL = not tied to a weekday.
    weekday    smallint    CHECK (weekday BETWEEN 1 AND 7),
    position   integer     NOT NULL DEFAULT 0,
    note       text        NOT NULL DEFAULT '',
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_routines_owner ON routines (owner_id, weekday);

DROP TRIGGER IF EXISTS routines_touch_updated_at ON routines;
CREATE TRIGGER routines_touch_updated_at
    BEFORE UPDATE ON routines
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

CREATE TABLE IF NOT EXISTS routine_exercises (
    id                      uuid     PRIMARY KEY DEFAULT gen_random_uuid(),
    routine_id              uuid     NOT NULL REFERENCES routines (id) ON DELETE CASCADE,
    -- RESTRICT: an exercise that a routine uses is archived, not deleted.
    exercise_id             text     NOT NULL REFERENCES exercises (id) ON DELETE RESTRICT,
    position                integer  NOT NULL,
    target_sets             integer  NOT NULL DEFAULT 3 CHECK (target_sets BETWEEN 1 AND 50),
    target_reps_min         integer  CHECK (target_reps_min BETWEEN 1 AND 1000),
    target_reps_max         integer  CHECK (target_reps_max BETWEEN 1 AND 1000),
    target_duration_seconds integer  CHECK (target_duration_seconds BETWEEN 1 AND 86400),
    rest_seconds            integer  NOT NULL DEFAULT 90 CHECK (rest_seconds BETWEEN 0 AND 3600),
    note                    text     NOT NULL DEFAULT '',
    CHECK (target_reps_min IS NULL OR target_reps_max IS NULL OR target_reps_min <= target_reps_max),
    UNIQUE (routine_id, position)
);
