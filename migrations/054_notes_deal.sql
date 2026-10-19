-- Migration 054: the deal of the practice a note is about (meeting notes, a
-- lead, an estimate), as task_items.deal_id (migration 053). Additive and
-- nullable; deleting the deal unlinks the note, the note stays.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE notes ADD COLUMN IF NOT EXISTS deal_id uuid REFERENCES practice_deals (id) ON DELETE SET NULL;
CREATE INDEX IF NOT EXISTS notes_deal ON notes (deal_id) WHERE deal_id IS NOT NULL;
