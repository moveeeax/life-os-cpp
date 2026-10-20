// Habits: the words of a habit row, its value, the seven cells of a week, the
// numbers of the day, the body of the habit form, the daily target, and the
// words of a mood check-in.
import { shiftDays } from '@/lib/dates';

import type { Habit, HabitDays, HabitInput, MoodCheckin, MoodInput, MoodTag } from './types';

export type * from './types';

export type Source = Habit['source'];

/** What ticks a habit, as the form offers it. */
export const SOURCE_LABEL: Record<Source, string> = {
  manual: 'By hand',
  sleep: 'Sleep from Mi',
  steps: 'Steps from Mi',
  active_kcal: 'Active kcal from Mi',
  workout: 'Training session',
  weight: 'Weigh-in on the Mi scales',
};

/** The unit a source's goal is in. */
export const GOAL_UNIT: Record<Source, string> = {
  manual: '',
  sleep: 'h',
  steps: 'steps',
  active_kcal: 'kcal',
  workout: '',
  weight: '',
};

/** Whether a source ticks the day by being there at all (a session, a weigh-in): it takes no goal. */
export const takesNoGoal = (s: Source): boolean => s === 'workout' || s === 'weight';

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

/** A day's value in the habit's own terms: "7 h 42 m", "9,312", "512 kcal", "2 sessions", "71.8 kg", "45 min". */
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
    case 'weight':
      return `${n1.format(value)} kg`;
    default:
      return h.unit ? `${n1.format(value)} ${h.unit}` : n1.format(value);
  }
}

/** The goal in words: "8 h", "8,000 steps", "45 min"; none for a workout or a weigh-in. */
export function goalLine(h: Pick<Habit, 'source' | 'goal' | 'unit'>): string | null {
  if (h.goal == null || takesNoGoal(h.source)) return null;
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

export type Cell = 'manual' | 'auto' | 'partial' | 'open' | 'future';

/**
 * The cell of each day of the week: ticked by hand, ticked from data, ticked
 * but short of the daily target, open, or after today. A day the answer does
 * not carry is open (or future).
 */
export function weekCells(h: Pick<HabitDays, 'days'>, monday: string, today: string): Cell[] {
  const byDate = new Map(h.days.map((d) => [d.date, d]));
  return weekDates(monday).map((date) => {
    if (date > today) return 'future';
    const d = byDate.get(date);
    if (d && !d.done && d.manual) return 'partial';
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
  target: string;
}

export function habitForm(h?: Habit): HabitForm {
  return {
    name: h?.name ?? '',
    schedule: h?.schedule ?? 'daily',
    per_week: h?.per_week != null ? String(h.per_week) : '3',
    goal: h?.goal != null ? String(h.goal) : '',
    unit: h?.unit ?? '',
    source: h?.source ?? 'manual',
    target: h?.target != null ? String(h.target) : '',
  };
}

/**
 * The body of the habit form (pinned by habits.test.ts); a workout or a weigh-in sends no goal
 * (the API refuses one on a weigh-in), only a habit by hand a target.
 */
export function habitBody(f: HabitForm): HabitInput {
  const goal = f.goal.trim() === '' || takesNoGoal(f.source) ? null : Number(f.goal);
  return {
    name: f.name.trim(),
    schedule: f.schedule,
    per_week: f.schedule === 'weekly' ? Number(f.per_week) : null,
    goal,
    unit: f.source === 'manual' ? f.unit.trim() : '',
    source: f.source,
    target: f.source === 'manual' && f.target.trim() !== '' ? Number(f.target) : null,
  };
}

/** What is wrong with the form before it is sent, or null. */
export function habitProblem(f: HabitForm): string | null {
  if (f.name.trim() === '') return 'Give the habit a name.';
  const needsGoal = f.source === 'sleep' || f.source === 'steps' || f.source === 'active_kcal';
  if (needsGoal && f.goal.trim() === '') return 'A source from Mi needs a goal.';
  if (f.goal.trim() !== '' && !takesNoGoal(f.source)) {
    const g = Number(f.goal);
    if (!Number.isFinite(g) || g <= 0) return 'The goal is a number above zero.';
    if (f.source === 'sleep' && g > 24) return 'A sleep goal is in hours, at most 24.';
  }
  if (f.source === 'manual' && f.target.trim() !== '') {
    const t = Number(f.target);
    if (!Number.isFinite(t) || t <= 0 || t > 1_000_000) {
      return 'The daily target is a number above zero, at most 1,000,000.';
    }
  }
  return null;
}

// ── the daily target ────────────────────────────────────────────────────────

/** Whether a habit fills up over the day (a target by hand) instead of a single tick. */
export const hasTarget = (h: Pick<Habit, 'source' | 'target'>): boolean =>
  h.source === 'manual' && h.target != null;

/** The target in words: "2,000 ml"; null without one. */
export function targetLine(h: Pick<Habit, 'unit' | 'target'>): string | null {
  if (h.target == null) return null;
  return h.unit ? `${n1.format(h.target)} ${h.unit}` : n1.format(h.target);
}

/** "750 of 2,000 ml": the day's sum against the target. */
export function progressLine(h: Pick<Habit, 'unit' | 'target'>, value: number | null | undefined) {
  const of = `${n1.format(value ?? 0)} of ${n1.format(h.target ?? 0)}`;
  return h.unit ? `${of} ${h.unit}` : of;
}

/** The amounts offered as one tap: a glass and a bottle for ml, else none. */
export function quickAmounts(h: Pick<Habit, 'unit'>): number[] {
  const unit = h.unit.trim().toLowerCase();
  if (unit === 'ml') return [250, 500];
  if (unit === 'l') return [0.25, 0.5];
  return [];
}

/** What is wrong with an amount to add, or null. */
export function amountProblem(text: string): string | null {
  const v = Number(text.replace(',', '.'));
  if (text.trim() === '' || !Number.isFinite(v) || v <= 0) return 'An amount above zero.';
  if (v > 1_000_000) return 'At most 1,000,000 at a time.';
  return null;
}

// ── mood ────────────────────────────────────────────────────────────────────

/** The tags of a check-in in the order the API keeps them (MoodTag in docs/openapi.yaml). */
export const MOOD_TAGS: MoodTag[] = [
  'calm',
  'happy',
  'energetic',
  'focused',
  'grateful',
  'social',
  'tired',
  'stressed',
  'anxious',
  'sad',
  'irritable',
  'sick',
];

/** The word of each score, 1 to 5. */
export const MOOD_WORDS: Record<number, string> = {
  1: 'Very low',
  2: 'Low',
  3: 'Okay',
  4: 'Good',
  5: 'Great',
};

export const moodWord = (score: number): string => MOOD_WORDS[score] ?? '';

/** The tags as one line: "calm, focused". */
export const tagLine = (tags: readonly string[]): string => tags.join(', ');

/** The check-in of @p date among @p list. */
export function moodOf(list: MoodCheckin[] | undefined, date: string): MoodCheckin | undefined {
  return list?.find((m) => m.date === date);
}

export interface MoodForm {
  score: number | null;
  tags: MoodTag[];
  note: string;
}

export function moodForm(m?: MoodCheckin): MoodForm {
  return { score: m?.score ?? null, tags: m ? [...m.tags] : [], note: m?.note ?? '' };
}

/** Adds or takes off a tag, the set's order kept. */
export function toggleTag(tags: MoodTag[], tag: MoodTag): MoodTag[] {
  const on = tags.includes(tag) ? tags.filter((t) => t !== tag) : [...tags, tag];
  return MOOD_TAGS.filter((t) => on.includes(t));
}

/** The body of the mood form; null until a score is picked. */
export function moodBody(f: MoodForm): MoodInput | null {
  if (f.score == null) return null;
  return { score: f.score, tags: f.tags, note: f.note.trim() };
}
