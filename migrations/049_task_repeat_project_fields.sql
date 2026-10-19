-- Migration 049: repeating tasks, and a deadline and a priority on projects.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

-- The repeat rule of a task (src/tasks/Repeat.hpp checks its shape): closing
-- the task creates the next occurrence, and recur_next_id points at it, so a
-- task closed, reopened and closed again does not create a second one.
ALTER TABLE task_items ADD COLUMN IF NOT EXISTS repeat jsonb;
ALTER TABLE task_items ADD COLUMN IF NOT EXISTS recur_next_id uuid REFERENCES task_items (id) ON DELETE SET NULL;
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'task_items_repeat_check') THEN
        ALTER TABLE task_items ADD CONSTRAINT task_items_repeat_check CHECK (
            repeat IS NULL OR (jsonb_typeof(repeat) = 'object'
                AND repeat->>'kind' IN ('daily', 'weekly', 'monthly', 'every_n_days')));
    END IF;
END $$;

-- A project's deadline and priority; a course keeps its own dates and has neither.
ALTER TABLE projects ADD COLUMN IF NOT EXISTS deadline date;
ALTER TABLE projects ADD COLUMN IF NOT EXISTS priority text;
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'projects_priority_check') THEN
        ALTER TABLE projects ADD CONSTRAINT projects_priority_check CHECK (priority IN ('p1', 'p2', 'p3'));
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'projects_course_no_deadline_check') THEN
        ALTER TABLE projects ADD CONSTRAINT projects_course_no_deadline_check
            CHECK (kind = 'project' OR (deadline IS NULL AND priority IS NULL));
    END IF;
END $$;
