-- Migration 014: fitness_read_indexes
--
-- Read routes filter samples by a half-open time interval. The uniqueness
-- keys start with user_id and do not serve a range on time alone; over a
-- quarter that is a seq scan across tens of thousands of rows per request.
--
-- The MigrationRunner wraps this file in ONE transaction. No BEGIN/COMMIT.

CREATE INDEX IF NOT EXISTS idx_heart_rate_time ON heart_rate_samples (timestamp);
CREATE INDEX IF NOT EXISTS idx_stress_time ON stress_samples (timestamp);
CREATE INDEX IF NOT EXISTS idx_spo2_time ON spo2_samples (timestamp);
CREATE INDEX IF NOT EXISTS idx_body_time ON body_measurements (timestamp);
CREATE INDEX IF NOT EXISTS idx_workouts_start ON workouts (start_at);
CREATE INDEX IF NOT EXISTS idx_sleep_end ON sleep_sessions (end_at);
CREATE INDEX IF NOT EXISTS idx_activity_date ON daily_activity (date);
