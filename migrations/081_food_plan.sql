-- Migration 081: the meal plan. A template per weekday (1 = Monday ... 7 =
-- Sunday) and an optional override for one day; a day reads its override,
-- else its weekday's template. Meals are a JSON array of
-- {time "HH:MM", title, kcal, protein_g?, meal}, kept sorted by time.

CREATE TABLE IF NOT EXISTS food_plan_templates (
    owner_id   uuid        NOT NULL,
    weekday    smallint    NOT NULL CHECK (weekday BETWEEN 1 AND 7),
    meals      jsonb       NOT NULL DEFAULT '[]'::jsonb CHECK (jsonb_typeof(meals) = 'array'),
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, weekday)
);

CREATE TABLE IF NOT EXISTS food_plan_days (
    owner_id   uuid        NOT NULL,
    day        date        NOT NULL,
    meals      jsonb       NOT NULL DEFAULT '[]'::jsonb CHECK (jsonb_typeof(meals) = 'array'),
    updated_at timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, day)
);

DROP TRIGGER IF EXISTS food_plan_templates_touch_updated_at ON food_plan_templates;
CREATE TRIGGER food_plan_templates_touch_updated_at BEFORE UPDATE ON food_plan_templates
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS food_plan_days_touch_updated_at ON food_plan_days;
CREATE TRIGGER food_plan_days_touch_updated_at BEFORE UPDATE ON food_plan_days
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();
