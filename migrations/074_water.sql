-- Migration 074: water drunk per local day (the "+250 ml" of Now) and the
-- day's goal in the food goals (2000 ml when unset).

CREATE TABLE IF NOT EXISTS food_water (
    owner_id   uuid        NOT NULL,
    day        date        NOT NULL,
    ml         integer     NOT NULL DEFAULT 0 CHECK (ml BETWEEN 0 AND 20000),
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, day)
);

DROP TRIGGER IF EXISTS food_water_touch_updated_at ON food_water;
CREATE TRIGGER food_water_touch_updated_at BEFORE UPDATE ON food_water
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

ALTER TABLE food_goals ADD COLUMN IF NOT EXISTS water_ml integer CHECK (water_ml BETWEEN 500 AND 8000);
