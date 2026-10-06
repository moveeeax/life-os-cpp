-- Migration 022: food — products, the diary, goals and LLM parse jobs of the
-- food module.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.
--
-- Every row belongs to a user (owner_id). An entry copies the numbers of its
-- item at write time: editing a product later must not rewrite the diary.

-- A product or a dish: own, copied from Open Food Facts, or made by an LLM parse.
CREATE TABLE IF NOT EXISTS food_items (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    source      text        NOT NULL DEFAULT 'custom' CHECK (source IN ('custom', 'off', 'ai')),
    off_code    text,
    name        text        NOT NULL CHECK (length(name) BETWEEN 1 AND 200),
    brand       text        NOT NULL DEFAULT '',
    -- What the nutrient columns refer to.
    per         text        NOT NULL DEFAULT '100g' CHECK (per IN ('100g', '100ml')),
    kcal        numeric     NOT NULL CHECK (kcal >= 0),
    protein_g   numeric     NOT NULL DEFAULT 0 CHECK (protein_g >= 0),
    fat_g       numeric     NOT NULL DEFAULT 0 CHECK (fat_g >= 0),
    carbs_g     numeric     NOT NULL DEFAULT 0 CHECK (carbs_g >= 0),
    fiber_g     numeric     CHECK (fiber_g >= 0),
    sugar_g     numeric     CHECK (sugar_g >= 0),
    salt_g      numeric     CHECK (salt_g >= 0),
    -- [{label, grams}], e.g. [{"label": "1 piece", "grams": 60}].
    servings    jsonb       NOT NULL DEFAULT '[]'::jsonb,
    -- Hidden from search; kept because entries may reference it.
    archived    boolean     NOT NULL DEFAULT false,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now()
);
CREATE UNIQUE INDEX IF NOT EXISTS food_items_owner_off_code
    ON food_items (owner_id, off_code) WHERE off_code IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_food_items_owner_name ON food_items (owner_id, lower(name));

-- One line of the diary.
CREATE TABLE IF NOT EXISTS food_entries (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    -- The day in the user's local zone, as chosen on the page.
    date        date        NOT NULL,
    meal        text        NOT NULL CHECK (meal IN ('breakfast', 'lunch', 'dinner', 'snack')),
    -- NULL for a quick entry; a deleted item leaves its entries with their numbers.
    item_id     uuid        REFERENCES food_items (id) ON DELETE SET NULL,
    name        text        NOT NULL CHECK (length(name) BETWEEN 1 AND 200),
    grams       numeric     CHECK (grams > 0),
    kcal        numeric     NOT NULL CHECK (kcal >= 0),
    protein_g   numeric     NOT NULL DEFAULT 0 CHECK (protein_g >= 0),
    fat_g       numeric     NOT NULL DEFAULT 0 CHECK (fat_g >= 0),
    carbs_g     numeric     NOT NULL DEFAULT 0 CHECK (carbs_g >= 0),
    fiber_g     numeric     CHECK (fiber_g >= 0),
    sugar_g     numeric     CHECK (sugar_g >= 0),
    salt_g      numeric     CHECK (salt_g >= 0),
    -- An LLM estimate or a quick entry, as opposed to a product's numbers.
    estimated   boolean     NOT NULL DEFAULT false,
    note        text        NOT NULL DEFAULT '',
    position    integer     NOT NULL DEFAULT 1,
    logged_at   timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_food_entries_owner_day ON food_entries (owner_id, date, meal, position);
CREATE INDEX IF NOT EXISTS idx_food_entries_item ON food_entries (item_id);

-- Profile and overrides of the daily goals; the computed numbers are not stored.
CREATE TABLE IF NOT EXISTS food_goals (
    owner_id           uuid        PRIMARY KEY,
    height_cm          integer     CHECK (height_cm BETWEEN 100 AND 250),
    birth_date         date,
    sex                text        CHECK (sex IN ('male', 'female')),
    activity           text        NOT NULL DEFAULT 'light'
                                   CHECK (activity IN ('sedentary', 'light', 'moderate', 'active', 'very_active')),
    target_weight_kg   numeric     CHECK (target_weight_kg BETWEEN 30 AND 300),
    -- Positive loses weight, 0 maintains, negative gains.
    pace_kg_per_week   numeric     NOT NULL DEFAULT 0 CHECK (pace_kg_per_week BETWEEN -1 AND 1.5),
    -- Used only when the Mi scale has no measurement.
    manual_weight_kg   numeric     CHECK (manual_weight_kg BETWEEN 30 AND 300),
    -- Free text the LLM parse gets about the user's eating habits.
    profile_note       text        NOT NULL DEFAULT '',
    kcal_override      integer     CHECK (kcal_override BETWEEN 500 AND 10000),
    protein_override_g integer     CHECK (protein_override_g BETWEEN 0 AND 1000),
    fat_override_g     integer     CHECK (fat_override_g BETWEEN 0 AND 1000),
    carbs_override_g   integer     CHECK (carbs_override_g BETWEEN 0 AND 2000),
    updated_at         timestamptz NOT NULL DEFAULT now()
);

-- A text description sent to the LLM by the food_parse job.
CREATE TABLE IF NOT EXISTS food_parse_jobs (
    id                uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id          uuid        NOT NULL,
    text              text        NOT NULL CHECK (length(text) BETWEEN 1 AND 2000),
    meal              text        NOT NULL CHECK (meal IN ('breakfast', 'lunch', 'dinner', 'snack')),
    date              date        NOT NULL,
    status            text        NOT NULL DEFAULT 'queued' CHECK (status IN ('queued', 'running', 'done', 'failed')),
    result            jsonb,
    error             text,
    model             text,
    prompt_tokens     integer,
    completion_tokens integer,
    created_at        timestamptz NOT NULL DEFAULT now(),
    finished_at       timestamptz
);
CREATE INDEX IF NOT EXISTS idx_food_parse_jobs_owner ON food_parse_jobs (owner_id, created_at DESC);
