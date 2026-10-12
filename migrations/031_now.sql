-- Migration 031: now — the shared wallpaper library (objects in S3, rows
-- here) and the focus of the day per user.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS wallpapers (
    id            uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    kind          text        NOT NULL CHECK (kind IN ('video', 'image')),
    device        text        NOT NULL CHECK (device IN ('desktop', 'phone')),
    object_key    text        NOT NULL UNIQUE,
    poster_key    text,
    content_type  text        NOT NULL,
    bytes         bigint      NOT NULL CHECK (bytes > 0),
    poster_bytes  bigint      CHECK (poster_bytes > 0),
    width         integer     CHECK (width > 0),
    height        integer     CHECK (height > 0),
    status        text        NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'ready')),
    created_by    uuid        NOT NULL,
    created_at    timestamptz NOT NULL DEFAULT now(),
    CHECK ((kind = 'video') = (poster_key IS NOT NULL)),
    CHECK ((poster_key IS NULL) = (poster_bytes IS NULL)),
    CHECK (device = 'desktop' OR kind = 'image')
);
CREATE INDEX IF NOT EXISTS idx_wallpapers_ready ON wallpapers (device) WHERE status = 'ready';

CREATE TABLE IF NOT EXISTS day_focus (
    owner_id    uuid        NOT NULL,
    date        date        NOT NULL,
    kind        text        NOT NULL CHECK (kind IN ('task', 'goal', 'text')),
    ref_id      uuid,
    text        text        NOT NULL CHECK (length(text) BETWEEN 1 AND 200),
    done_at     timestamptz,
    updated_at  timestamptz NOT NULL DEFAULT now(),
    PRIMARY KEY (owner_id, date),
    CHECK ((kind = 'text') = (ref_id IS NULL))
);
