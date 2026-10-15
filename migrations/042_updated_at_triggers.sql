-- Migration 042: the shared touch_updated_at() trigger on every table with an
-- updated_at column that had none.
--
-- docs/CONVENTIONS.md asks every resource table to attach the trigger from
-- migration 000 instead of setting updated_at by hand; 007, 008, 017 and 019
-- did, the modules after them bumped the column in each UPDATE statement, and
-- five repositories forgot (project review, wave 3). With the trigger in place
-- the repositories drop their manual `updated_at = now()`; a BEFORE UPDATE
-- trigger also fires on the DO UPDATE path of INSERT ... ON CONFLICT.
--
-- Expand-only: the previous binary still sets the column by hand, which the
-- trigger overwrites with the same now().
--
-- NOTE: no BEGIN/COMMIT — MigrationRunner wraps the file in one transaction.

DROP TRIGGER IF EXISTS wallet_balances_touch_updated_at ON wallet_balances;
CREATE TRIGGER wallet_balances_touch_updated_at
    BEFORE UPDATE ON wallet_balances
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- fitness (012)
DROP TRIGGER IF EXISTS daily_activity_touch_updated_at ON daily_activity;
CREATE TRIGGER daily_activity_touch_updated_at
    BEFORE UPDATE ON daily_activity
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS sleep_sessions_touch_updated_at ON sleep_sessions;
CREATE TRIGGER sleep_sessions_touch_updated_at
    BEFORE UPDATE ON sleep_sessions
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS workouts_touch_updated_at ON workouts;
CREATE TRIGGER workouts_touch_updated_at
    BEFORE UPDATE ON workouts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS body_measurements_touch_updated_at ON body_measurements;
CREATE TRIGGER body_measurements_touch_updated_at
    BEFORE UPDATE ON body_measurements
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS heart_rate_samples_touch_updated_at ON heart_rate_samples;
CREATE TRIGGER heart_rate_samples_touch_updated_at
    BEFORE UPDATE ON heart_rate_samples
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS spo2_samples_touch_updated_at ON spo2_samples;
CREATE TRIGGER spo2_samples_touch_updated_at
    BEFORE UPDATE ON spo2_samples
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS stress_samples_touch_updated_at ON stress_samples;
CREATE TRIGGER stress_samples_touch_updated_at
    BEFORE UPDATE ON stress_samples
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS abnormal_heart_beat_events_touch_updated_at ON abnormal_heart_beat_events;
CREATE TRIGGER abnormal_heart_beat_events_touch_updated_at
    BEFORE UPDATE ON abnormal_heart_beat_events
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- food (022)
DROP TRIGGER IF EXISTS food_items_touch_updated_at ON food_items;
CREATE TRIGGER food_items_touch_updated_at
    BEFORE UPDATE ON food_items
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS food_goals_touch_updated_at ON food_goals;
CREATE TRIGGER food_goals_touch_updated_at
    BEFORE UPDATE ON food_goals
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- money (025)
DROP TRIGGER IF EXISTS money_accounts_touch_updated_at ON money_accounts;
CREATE TRIGGER money_accounts_touch_updated_at
    BEFORE UPDATE ON money_accounts
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS money_categories_touch_updated_at ON money_categories;
CREATE TRIGGER money_categories_touch_updated_at
    BEFORE UPDATE ON money_categories
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS money_transactions_touch_updated_at ON money_transactions;
CREATE TRIGGER money_transactions_touch_updated_at
    BEFORE UPDATE ON money_transactions
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS money_transfers_touch_updated_at ON money_transfers;
CREATE TRIGGER money_transfers_touch_updated_at
    BEFORE UPDATE ON money_transfers
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS money_settings_touch_updated_at ON money_settings;
CREATE TRIGGER money_settings_touch_updated_at
    BEFORE UPDATE ON money_settings
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- tasks (029), projects (038)
DROP TRIGGER IF EXISTS task_items_touch_updated_at ON task_items;
CREATE TRIGGER task_items_touch_updated_at
    BEFORE UPDATE ON task_items
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS task_notes_touch_updated_at ON task_notes;
CREATE TRIGGER task_notes_touch_updated_at
    BEFORE UPDATE ON task_notes
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS projects_touch_updated_at ON projects;
CREATE TRIGGER projects_touch_updated_at
    BEFORE UPDATE ON projects
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- goals (030)
DROP TRIGGER IF EXISTS goal_items_touch_updated_at ON goal_items;
CREATE TRIGGER goal_items_touch_updated_at
    BEFORE UPDATE ON goal_items
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- now (031, 036)
DROP TRIGGER IF EXISTS day_focus_touch_updated_at ON day_focus;
CREATE TRIGGER day_focus_touch_updated_at
    BEFORE UPDATE ON day_focus
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS now_settings_touch_updated_at ON now_settings;
CREATE TRIGGER now_settings_touch_updated_at
    BEFORE UPDATE ON now_settings
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS day_note_touch_updated_at ON day_note;
CREATE TRIGGER day_note_touch_updated_at
    BEFORE UPDATE ON day_note
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- travel (033)
DROP TRIGGER IF EXISTS travel_settings_touch_updated_at ON travel_settings;
CREATE TRIGGER travel_settings_touch_updated_at
    BEFORE UPDATE ON travel_settings
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

DROP TRIGGER IF EXISTS travel_trips_touch_updated_at ON travel_trips;
CREATE TRIGGER travel_trips_touch_updated_at
    BEFORE UPDATE ON travel_trips
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();

-- notes (039)
DROP TRIGGER IF EXISTS notes_touch_updated_at ON notes;
CREATE TRIGGER notes_touch_updated_at
    BEFORE UPDATE ON notes
    FOR EACH ROW EXECUTE FUNCTION touch_updated_at();
