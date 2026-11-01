import { QueryClientContext, type QueryClient } from '@tanstack/react-query';
import { useContext, useRef } from 'react';

import { FOOD_DIARY_WRITES } from '@/hooks/useFood';
import { HABIT_WRITES } from '@/hooks/useHabits';
import { ROW_WRITES } from '@/hooks/useMoney';
import { TASK_WRITES } from '@/hooks/useTasks';
import { qk } from '@/lib/api/queryKeys';

import type { Action } from './chat';

/**
 * What Ani changed on the person's behalf: a chip of hers (a dragged task, a
 * transaction's category or its "one-off" note, a meal, a fact, a thought for
 * the inbox) or a proposal confirmed in the chat or her bubble. Now stays
 * mounted under every tab and does not refetch on focus, so without this the
 * section keeps the old rows until a reload.
 */
export type Changed = 'task' | 'transaction' | 'food' | 'fact' | Action['kind'];

type Keys = readonly (readonly unknown[])[];

const AFTER: Record<Changed, Keys> = {
  task: TASK_WRITES,
  create_task: TASK_WRITES,
  complete_task: TASK_WRITES,
  // The inbox of Tasks (POST /tasks/notes), counted on Now.
  add_note: TASK_WRITES,
  // Her own unusual-spend check reads today's rows under its own key.
  transaction: [...ROW_WRITES, ['ani', 'life', 'tx-today']],
  food: [...FOOD_DIARY_WRITES, ['ani', 'life', 'food-day']],
  check_habit: HABIT_WRITES,
  fact: [qk.ani.facts()],
  journal_note: [qk.notes.all()],
  calendar_add: [qk.calendar(), qk.now.ribbon()],
  calendar_move: [qk.calendar(), qk.now.ribbon()],
  calendar_remove: [qk.calendar(), qk.now.ribbon()],
  tg_send: [qk.telegramWaiting()],
  tg_edit: [qk.telegramWaiting()],
};

/** The query keys (prefixes) a change of @p what makes stale. */
export function keysAfter(what: Changed): Keys {
  return AFTER[what] ?? [];
}

/** Marks what @p what changed as stale, so the open section and Now read it again. */
export function refreshAfter(qc: QueryClient, what: Changed): void {
  for (const queryKey of keysAfter(what)) void qc.invalidateQueries({ queryKey });
}

/**
 * refreshAfter on the page's query client. Read from the context, not
 * useQueryClient: a corner drawn without a provider (a test) has nothing to refresh.
 */
export function useRefresh(): (what: Changed) => void {
  const qc = useContext(QueryClientContext);
  const ref = useRef(qc);
  ref.current = qc;
  return useRef((what: Changed) => {
    if (ref.current) refreshAfter(ref.current, what);
  }).current;
}
