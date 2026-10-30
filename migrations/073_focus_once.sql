-- 073: a focus session is kept once per start, so two tabs ending the same
-- session record it once (POST /api/v1/now/focus-sessions answers with the
-- same row).
DELETE FROM focus_sessions a USING focus_sessions b
 WHERE a.owner_id = b.owner_id AND a.started_at = b.started_at AND a.id > b.id;

CREATE UNIQUE INDEX IF NOT EXISTS focus_sessions_owner_start ON focus_sessions (owner_id, started_at);
