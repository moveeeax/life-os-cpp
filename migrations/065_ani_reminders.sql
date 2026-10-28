-- Migration 065: Ani's recurring reminders. A reminder says its text at a
-- local time (at_time in tz) on the weekdays of its mask (Mon=1, Tue=2,
-- Wed=4, Thu=8, Fri=16, Sat=32, Sun=64). next_at is when it is due next;
-- the job ani_reminders_due puts a remind item in her inbox then and moves
-- next_at on (from now, so a long pause catches up once, not in bulk).

CREATE TABLE IF NOT EXISTS ani_reminders (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid NOT NULL,
    text        text NOT NULL CHECK (char_length(text) BETWEEN 1 AND 200),
    at_time     time NOT NULL,
    days        integer NOT NULL CHECK (days BETWEEN 1 AND 127),
    tz          text NOT NULL,
    next_at     timestamptz NOT NULL,
    created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS ani_reminders_owner ON ani_reminders (owner_id, created_at);
CREATE INDEX IF NOT EXISTS ani_reminders_due ON ani_reminders (next_at);
