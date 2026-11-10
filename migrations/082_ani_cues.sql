-- Migration 082: Ani's cues on the screens of the app (GET /api/v1/ani/cues).
--   ani_cue_dismissals: a cue the person sent away, hidden for the rest of
--     that local day; the rules' cue ids are stable per rule, so one row per
--     owner, day and id is enough.
--   ani_cues: a cue her agent wrote itself (the set_cue tool), one per owner,
--     local day and screen; a new one on the same screen replaces it with a
--     new id, so a dismissed one does not hide its successor.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock: do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS ani_cue_dismissals (
    owner_id    uuid        NOT NULL,
    day         date        NOT NULL,
    cue_id      text        NOT NULL CHECK (char_length(cue_id) BETWEEN 1 AND 64),
    created_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, day, cue_id)
);

CREATE TABLE IF NOT EXISTS ani_cues (
    id          uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    owner_id    uuid        NOT NULL,
    day         date        NOT NULL,
    screen      text        NOT NULL CHECK (screen IN ('today', 'food', 'body', 'money')),
    text        text        NOT NULL CHECK (char_length(text) BETWEEN 1 AND 200),
    -- {label, kind, payload?} or NULL for a cue without a button.
    action      jsonb       NULL,
    created_at  timestamptz NOT NULL DEFAULT now(),
    UNIQUE (owner_id, day, screen)
);
