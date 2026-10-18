// Habits: the words of a habit row, its value, the seven cells of a week, the
// numbers of the day, and the body of the habit form.
import { shiftDays } from '@/lib/dates';

import type { Habit, HabitDays, HabitInput } from './types';

export type * from './types';

export type Source = Habit['source'];

/** What ticks a habit, as the form offers it. */
export const SOURCE_LABEL: Record<Source, string> = {
  manual: 'By hand',
  sleep: 'Sleep from Mi',
  steps: 'Steps from Mi',
  active_kcal: 'Active kcal from Mi',
  workout: 'Training session',
};

/** The unit a source's goal is in. */
export const GOAL_UNIT: Record<Source, string> = {
  manual: '',
  sleep: 'h',
  steps: 'steps',
  active_kcal: 'kcal',
  workout: '',
};

const n0 = new Intl.NumberFormat('en-US', { maximumFractionDigits: 0 });
const n1 = new Intl.NumberFormat('en-US', { maximumFractionDigits: 1 });

/** "every day", "once a week", "twice a week", "3 times a week". */
export function scheduleWords(h: Pick<Habit, 'schedule' | 'per_week'>): string {
  if (h.schedule === 'daily') return 'every day';
  const n = h.per_week ?? 1;
  if (n === 1) return 'once a week';
  if (n === 2) return 'twice a week';
  return `${n} times a week`;
}

/** The line under a habit's name: where its ticks come from and how often. */
export function subline(
  h: Pick<Habit, 'schedule' | 'per_week' | 'source'> & { no_data?: boolean },
) {
  const when = scheduleWords(h);
  if (h.source === 'manual') return when;
  const from = h.source === 'workout' ? 'Training' : 'Mi';
  return h.no_data ? `no data from ${from} · ${when}` : `from ${from} · ${when}`;
}

/** A line cut into runs, the digits apart: "4 times a week" → "4" (num), " times a week". */
export function splitNumbers(text: string): { text: string; num: boolean }[] {
  return text
    .split(/(\d[\d,.]*)/)
    .filter((t) => t !== '')
    .map((t) => ({ text: t, num: /^\d/.test(t) }));
}

/** A day's value in the habit's own terms: "7 h 42 m", "9,312", "512 kcal", "2 sessions", "45 min". */
export function formatValue(h: Pick<Habit, 'source' | 'unit'>, value: number): string {
  switch (h.source) {
    case 'sleep': {
      const m = Math.round(value);
      return m < 60 ? `${m} m` : `${Math.floor(m / 60)} h ${m % 60} m`;
    }
    case 'steps':
      return n0.format(value);
    case 'active_kcal':
      return `${n0.format(value)} kcal`;
    case 'workout':
      return value === 1 ? '1 session' : `${n0.format(value)} sessions`;
    default:
      return h.unit ? `${n1.format(value)} ${h.unit}` : n1.format(value);
  }
}

/** The goal in words: "8 h", "8,000 steps", "45 min"; none for a workout. */
export function goalLine(h: Pick<Habit, 'source' | 'goal' | 'unit'>): string | null {
  if (h.goal == null || h.source === 'workout') return null;
  if (h.source === 'sleep') return `${n1.format(h.goal)} h`;
  if (h.source === 'steps') return `${n0.format(h.goal)} steps`;
  if (h.source === 'active_kcal') return `${n0.format(h.goal)} kcal`;
  return h.unit ? `${n1.format(h.goal)} ${h.unit}` : n1.format(h.goal);
}

const shift = shiftDays;

export const addDays = shift;

export { mondayOf } from '@/lib/dates';

/** The seven days of the week that starts on @p monday. */
export function weekDates(monday: string): string[] {
  return Array.from({ length: 7 }, (_, i) => shift(monday, i));
}

/** "Wednesday, Oct 14". */
export function longDay(iso: string): string {
  return new Date(`${iso}T00:00:00Z`).toLocaleDateString('en-US', {
    weekday: 'long',
    month: 'short',
    day: 'numeric',
    timeZone: 'UTC',
  });
}

/** "Oct 12 – Oct 18". */
export function weekLine(monday: string): string {
  const f = (iso: string) =>
    new Date(`${iso}T00:00:00Z`).toLocaleDateString('en-US', {
      month: 'short',
      day: 'numeric',
      timeZone: 'UTC',
    });
  return `${f(monday)} – ${f(shift(monday, 6))}`;
}

/** How far back a tick reaches (kTickBackMax in HabitsController.cpp). */
export const TICK_BACK_DAYS = 366;

/** The oldest day a hand tick still reaches from @p today. */
export const oldestTickDay = (today: string): string => shift(today, -TICK_BACK_DAYS);

/** Whether the week before the one of @p monday still has a day a tick reaches. */
export const canGoBack = (monday: string, today: string): boolean =>
  shift(monday, -1) >= oldestTickDay(today);

export type Cell = 'manual' | 'auto' | 'open' | 'future';

/**
 * The cell of each day of the week: ticked by hand, ticked from data, open, or
 * after today. A day the answer does not carry is open (or future).
 */
export function weekCells(h: Pick<HabitDays, 'days'>, monday: string, today: string): Cell[] {
  const byDate = new Map(h.days.map((d) => [d.date, d]));
  return weekDates(monday).map((date) => {
    if (date > today) return 'future';
    const d = byDate.get(date);
    if (!d || !d.done) return 'open';
    return d.manual ? 'manual' : 'auto';
  });
}

/** The day of @p date in a habit's answer. */
export function dayOf(h: Pick<HabitDays, 'days'>, date: string) {
  return h.days.find((d) => d.date === date);
}

/**
 * The numbers of the day as the Now plate counts them: a daily habit done
 * today, a weekly one done today or with its week already met.
 */
export function dayCount(
  list: HabitDays[],
  today: string,
): { done: number; total: number; left: number } {
  let done = 0;
  let left = 0;
  for (const h of list) {
    const d = dayOf(h, today)?.done ?? false;
    if (h.schedule === 'daily') {
      if (d) done += 1;
      else left += 1;
    } else if (d || (h.week_done ?? 0) >= (h.per_week ?? 1)) {
      done += 1;
    }
  }
  return { done, total: list.length, left };
}

/** The done flags of the last @p n days up to @p today, oldest first. */
export function lastDays(h: Pick<HabitDays, 'days'>, today: string, n = 30): boolean[] {
  const byDate = new Map(h.days.map((d) => [d.date, d.done]));
  return Array.from({ length: n }, (_, i) => byDate.get(shift(today, i - n + 1)) ?? false);
}

export interface HabitForm {
  name: string;
  schedule: Habit['schedule'];
  per_week: string;
  goal: string;
  unit: string;
  source: Source;
}

export function habitForm(h?: Habit): HabitForm {
  return {
    name: h?.name ?? '',
    schedule: h?.schedule ?? 'daily',
    per_week: h?.per_week != null ? String(h.per_week) : '3',
    goal: h?.goal != null ? String(h.goal) : '',
    unit: h?.unit ?? '',
    source: h?.source ?? 'manual',
  };
}

/** The body of the habit form (pinned by habits.test.ts); a workout sends no goal. */
export function habitBody(f: HabitForm): HabitInput {
  const goal = f.goal.trim() === '' || f.source === 'workout' ? null : Number(f.goal);
  return {
    name: f.name.trim(),
    schedule: f.schedule,
    per_week: f.schedule === 'weekly' ? Number(f.per_week) : null,
    goal,
    unit: f.source === 'manual' ? f.unit.trim() : '',
    source: f.source,
  };
}

/** What is wrong with the form before it is sent, or null. */
export function habitProblem(f: HabitForm): string | null {
  if (f.name.trim() === '') return 'Give the habit a name.';
  const needsGoal = f.source === 'sleep' || f.source === 'steps' || f.source === 'active_kcal';
  if (needsGoal && f.goal.trim() === '') return 'A source from Mi needs a goal.';
  if (f.goal.trim() !== '' && f.source !== 'workout') {
    const g = Number(f.goal);
    if (!Number.isFinite(g) || g <= 0) return 'The goal is a number above zero.';
    if (f.source === 'sleep' && g > 24) return 'A sleep goal is in hours, at most 24.';
  }
  return null;
}
