import type { Today } from '@/lib/now/types';

/*
 * Her evening word with the day's tasks: how many of today's are done, and
 * an offer to move the open ones to tomorrow (the page asks with buttons).
 */

/** The local day after @p date (YYYY-MM-DD), as YYYY-MM-DD. */
export function tomorrowOf(date: string): string {
  const d = new Date(`${date}T12:00:00Z`);
  d.setUTCDate(d.getUTCDate() + 1);
  return d.toISOString().slice(0, 10);
}

/** The line and the open tasks to move; null without tasks today. */
export function evening(
  today: Today | undefined,
  hidden: boolean,
): { text: string; move: string[] } | null {
  const all = today?.tasks?.today ?? [];
  if (all.length === 0) return null;
  const move = all.filter((t) => t.status !== 'done').map((t) => t.id);
  if (move.length === 0) return { text: 'Все задачи на сегодня сделаны.', move };
  if (hidden) return { text: 'Остались задачи на сегодня. Перенести на завтра?', move };
  const done = all.length - move.length;
  return { text: `Сделано ${done} из ${all.length}. Перенести ${move.length} на завтра?`, move };
}
