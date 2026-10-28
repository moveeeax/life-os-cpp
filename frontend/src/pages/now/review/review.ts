import { useEffect, useState } from 'react';

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

/** Reads the week the review last opened by itself (null when unknown). */
function seenWeek(): string | null {
  try {
    return localStorage.getItem(SEEN_KEY);
  } catch {
    return null;
  }
}

/**
 * Whether the review is open: by itself on Sunday from 10:00 (checked every
 * minute, so a page left open since the morning opens it too; once a week,
 * announced with REVIEW_EVENT so Ani says her line), or from the account
 * menu (REVIEW_EVENT), which does not count as the week's own opening.
 */
export function useWeeklyReview(date: string) {
  const [open, setOpen] = useState(false);
  useEffect(() => {
    const onEvent = () => setOpen(true);
    const check = () => {
      if (!dueThisWeek(date, new Date().getHours(), seenWeek())) return;
      try {
        localStorage.setItem(SEEN_KEY, weekOf(date));
      } catch {
        // Private mode: it may open again next time.
      }
      window.dispatchEvent(new Event(REVIEW_EVENT));
    };
    window.addEventListener(REVIEW_EVENT, onEvent);
    check();
    const id = window.setInterval(check, 60_000);
    return () => {
      window.removeEventListener(REVIEW_EVENT, onEvent);
      clearInterval(id);
    };
  }, [date]);
  return { open, close: () => setOpen(false) };
}
