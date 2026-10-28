-- Migration 066: LeetCode in Life OS. A profile per user (the public
-- username and the zone its days are counted in) and one snapshot of the
-- solved counts per local day (the latest pull of that day wins). The day's
-- solved problems are the growth over the previous snapshot (the series
-- leetcode:solved of the metrics catalog). The public profile shows totals
-- only: its submission calendar may be private.

CREATE TABLE IF NOT EXISTS leetcode_profiles (
    owner_id    uuid PRIMARY KEY,
    username    text NOT NULL CHECK (username ~ '^[A-Za-z0-9_.-]{1,30}$'),
    tz          text NOT NULL,
    synced_at   timestamptz NULL,
    last_error  text NULL,
    created_at  timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS leetcode_snapshots (
    owner_id    uuid NOT NULL,
    day         date NOT NULL,
    total       integer NOT NULL CHECK (total >= 0),
    easy        integer NOT NULL CHECK (easy >= 0),
    medium      integer NOT NULL CHECK (medium >= 0),
    hard        integer NOT NULL CHECK (hard >= 0),
    taken_at    timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, day)
);
