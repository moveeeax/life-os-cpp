-- Migration 064: more of what Ani's webhook and MCP can ask for.
-- ani_inbox: the kinds mood, remind, ask, focus, card, wallpaper, sound and
-- hold; not_before (a reminder is not handed to the page before then);
-- answer / answered_at (an ask answered on the page) and callback /
-- callback_sent (where the answer goes, sent once). ani_prefs.hold_until:
-- nothing is handed to the page until then. ani_turns.proposal: a turn the
-- MCP opened to propose a task, a habit tick or a note ({tool, args}),
-- resolved by the worker without her agent.

DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ani_inbox_kind_check'
               AND pg_get_constraintdef(oid) NOT LIKE '%remind%') THEN
        ALTER TABLE ani_inbox DROP CONSTRAINT ani_inbox_kind_check;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ani_inbox_kind_check') THEN
        ALTER TABLE ani_inbox ADD CONSTRAINT ani_inbox_kind_check CHECK (kind IN (
            'say', 'outfit', 'act', 'event', 'mood', 'remind', 'ask', 'focus', 'card', 'wallpaper', 'sound', 'hold'));
    END IF;
END $$;

ALTER TABLE ani_inbox ADD COLUMN IF NOT EXISTS not_before timestamptz NULL;
ALTER TABLE ani_inbox ADD COLUMN IF NOT EXISTS answer text NULL;
ALTER TABLE ani_inbox ADD COLUMN IF NOT EXISTS answered_at timestamptz NULL;
ALTER TABLE ani_inbox ADD COLUMN IF NOT EXISTS callback text NULL;
ALTER TABLE ani_inbox ADD COLUMN IF NOT EXISTS callback_sent boolean NOT NULL DEFAULT false;

CREATE INDEX IF NOT EXISTS ani_inbox_callbacks ON ani_inbox (expires_at)
    WHERE callback IS NOT NULL AND NOT callback_sent;

ALTER TABLE ani_prefs ADD COLUMN IF NOT EXISTS hold_until timestamptz NULL;

ALTER TABLE ani_turns ADD COLUMN IF NOT EXISTS proposal jsonb NULL;
