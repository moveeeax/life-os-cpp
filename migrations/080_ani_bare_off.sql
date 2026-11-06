-- Migration 080: the bare outfit is gone (and the lingerie with it).
-- Turns it off for everyone; the column stays until a later release drops it
-- (expand and contract, migrations/README.md): the previous API binary still
-- reads bare_on_at while this one rolls out.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock — do NOT add BEGIN/COMMIT. Idempotent DML.

UPDATE ani_prefs SET bare_on_at = NULL WHERE bare_on_at IS NOT NULL;
