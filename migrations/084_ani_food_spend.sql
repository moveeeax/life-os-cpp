-- Migration 084: Ani's agent proposes food diary entries (add_food) and posted expenses
-- (add_spend); the user confirms them on Now like the other kinds, and the
-- worker writes them through POST /api/v1/food/entries/batch and
-- POST /api/v1/money/transactions/batch. Widening the check is an expand step:
-- the previous binary writes only kinds it already allowed.
--
-- Applied in numeric order on boot. The MigrationRunner wraps this file in ONE
-- transaction under an advisory lock: do NOT add BEGIN/COMMIT. Idempotent DDL.

ALTER TABLE ani_actions DROP CONSTRAINT IF EXISTS ani_actions_kind_check;
ALTER TABLE ani_actions ADD CONSTRAINT ani_actions_kind_check
    CHECK (kind IN ('create_task', 'complete_task', 'check_habit', 'add_note', 'tg_send', 'tg_edit',
                    'calendar_add', 'calendar_move', 'calendar_remove', 'journal_note', 'add_food', 'add_spend'));
