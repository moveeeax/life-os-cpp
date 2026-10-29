-- Migration 069: what the day ribbon on Now draws besides the calendar. A
-- task may have a time on its due day (HH:MM in the person's zone; none
-- without a day). A focus session is recorded when it ends: the timer stays
-- on the page, the server keeps what was done.

ALTER TABLE task_items ADD COLUMN IF NOT EXISTS due_time time NULL;

CREATE TABLE IF NOT EXISTS focus_sessions (
    id          uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid NOT NULL,
    started_at  timestamptz NOT NULL,
    ended_at    timestamptz NOT NULL,
    minutes     integer NOT NULL CHECK (minutes BETWEEN 1 AND 240),
    label       text NOT NULL DEFAULT '' CHECK (char_length(label) <= 200),
    created_at  timestamptz NOT NULL DEFAULT now(),
    CHECK (ended_at > started_at)
);

CREATE INDEX IF NOT EXISTS focus_sessions_owner_started ON focus_sessions (owner_id, started_at);
