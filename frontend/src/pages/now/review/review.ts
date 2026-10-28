import type { Today } from '@/lib/now/types';

/*
 * The weekly review on Now: on Sunday from 10:00 (once a week) or from the
 * account menu, a sheet with late tasks, goals behind their pace and
 * habits under their weekly target, each row closable for the week.
 */

/** Opens the review from anywhere on Now (the account menu). */
export const REVIEW_EVENT = 'now:review';
export const SEEN_KEY = 'now:review:seen';
export const closedKey = (week: string) => `now:review:closed:${week}`;

/** The ISO week of a local day (YYYY-MM-DD), as 2026-W44. */
export function weekOf(date: string): string {
  const d = new Date(`${date}T12:00:00Z`);
  const dow = (d.getUTCDay() + 6) % 7; // Monday 0
  d.setUTCDate(d.getUTCDate() - dow + 3); // the week's Thursday
  const year = d.getUTCFullYear();
  const start = Date.UTC(year, 0, 1);
  const week = Math.ceil(((d.getTime() - start) / 86_400_000 + 1) / 7);
  return `${year}-W${String(week).padStart(2, '0')}`;
}

/** Whether to open it by itself: a Sunday from 10:00, not yet this week. */
export function dueThisWeek(date: string, hour: number, seen: string | null): boolean {
  const sunday = new Date(`${date}T12:00:00Z`).getUTCDay() === 0;
  return sunday && hour >= 10 && seen !== weekOf(date);
}

export function groupsOf(today: Today | undefined) {
  return {
    late: today?.tasks?.late ?? [],
    behind: (today?.goals ?? []).filter((g) => g.pace === 'behind'),
    habits: (today?.habits?.week ?? []).filter((h) => h.done < h.per_week),
  };
}
