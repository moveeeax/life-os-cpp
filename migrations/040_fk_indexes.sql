-- Migration 040: indexes on foreign keys that had none.
--
-- Deleting a task sets notes.task_id and task_notes.task_id to NULL, and
-- deleting a goal cascades to goal_milestones: without an index on the
-- referencing column each of those deletes scans the whole child table.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

CREATE INDEX IF NOT EXISTS notes_task ON notes (task_id) WHERE task_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_task_notes_task ON task_notes (task_id) WHERE task_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_goal_milestones_goal ON goal_milestones (goal_id);
