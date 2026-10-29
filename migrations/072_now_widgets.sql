-- Migration 072: the widget board of Now, at most six widgets in order. Each
-- is {kind, source?}: kind series takes a metrics catalog id as its source;
-- the other kinds (calendar_week, habits, spend, telegram, trip) need none.

ALTER TABLE now_settings ADD COLUMN IF NOT EXISTS widgets jsonb NULL;
