-- Migration 032: sounds — the shared library of focus sounds (objects in S3,
-- rows here), uploaded by an admin like the wallpapers.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE TABLE IF NOT EXISTS sounds (
    id                uuid        PRIMARY KEY DEFAULT gen_random_uuid(),
    title             text        NOT NULL CHECK (length(title) BETWEEN 1 AND 80),
    object_key        text        NOT NULL UNIQUE,
    content_type      text        NOT NULL,
    bytes             bigint      NOT NULL CHECK (bytes > 0),
    duration_seconds  integer     CHECK (duration_seconds > 0),
    status            text        NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'ready')),
    created_by        uuid        NOT NULL,
    created_at        timestamptz NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_sounds_ready ON sounds (title) WHERE status = 'ready';
