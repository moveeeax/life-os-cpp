-- Migration 075: what Ani does on her own.
--   ani_deals: a promise the person makes to Ani for one week ("the gym three
--     times"), checked against workouts, focus minutes or a habit's ticks.
--   ani_actions gains journal_note: the evening diary she writes from a talk,
--     a note under the topic journal once the person confirms it.
--   telegram_link gains voice_to_self (the morning and evening line as a
--     voice message to Saved Messages) and voice_sent, the last day each went.

CREATE TABLE IF NOT EXISTS ani_deals (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    text        text        NOT NULL CHECK (char_length(text) BETWEEN 1 AND 200),
    kind        text        NOT NULL CHECK (kind IN ('workouts', 'focus_minutes', 'habit')),
    habit_id    uuid        NULL REFERENCES habits (id) ON DELETE CASCADE,
    target      integer     NOT NULL CHECK (target BETWEEN 1 AND 10000),
    week_start  date        NOT NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    updated_at  timestamptz NOT NULL DEFAULT now(),
    CHECK ((kind = 'habit') = (habit_id IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS ani_deals_owner_week ON ani_deals (owner_id, week_start);

DROP TRIGGER IF EXISTS ani_deals_touch_updated_at ON ani_deals;
CREATE TRIGGER ani_deals_touch_updated_at BEFORE UPDATE ON ani_deals
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

ALTER TABLE ani_actions DROP CONSTRAINT IF EXISTS ani_actions_kind_check;
ALTER TABLE ani_actions ADD CONSTRAINT ani_actions_kind_check
    CHECK (kind IN ('create_task', 'complete_task', 'check_habit', 'add_note', 'tg_send', 'tg_edit',
                    'calendar_add', 'calendar_move', 'calendar_remove', 'journal_note'));

ALTER TABLE telegram_link ADD COLUMN IF NOT EXISTS voice_to_self boolean NOT NULL DEFAULT false;
ALTER TABLE telegram_link ADD COLUMN IF NOT EXISTS voice_sent jsonb NOT NULL DEFAULT '{}'::jsonb;
