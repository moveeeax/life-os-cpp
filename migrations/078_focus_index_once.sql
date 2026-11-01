-- 078: focus_sessions had two indexes on (owner_id, started_at): the plain one of
-- 069 and the unique one of 073, which serves every read the plain one did.
-- Every insert kept both; the plain one goes.
DROP INDEX IF EXISTS focus_sessions_owner_started;
