// Goals on the page: the kind labels, the pace word of a row, values with
// their unit and the shelf of a count goal.
export * from './types';
import type { Goal, GoalCheckin, GoalKind } from './types';

export const KIND_LABEL: Record<GoalKind, string> = {
  number: 'number',
  steps: 'steps',
  count: 'count',
  binary: 'pass / fail',
};

/** A value with its unit: thousands grouped from 10 000 up, one decimal below. */
export function formatValue(v: number, unit: string): string {
  const text =
    Math.abs(v) >= 10_000 ? Math.round(v).toLocaleString('en-US') : String(Math.round(v * 10) / 10);
  return unit ? `${text} ${unit}` : text;
}

/**
 * The weekly pace fact of a number goal: "reached" once progress hits 1 (the
 * server clamps it, so past the target too), a sign only on a non-zero value.
 */
export function weeklyPace(
  progress: number | null | undefined,
  perWeek: number | null | undefined,
  unit: string,
): { value: string; label: string } {
  if ((progress ?? 0) >= 1) return { value: 'reached', label: 'the target' };
  const v = perWeek ?? 0;
  const shown = formatValue(Math.abs(v), unit);
  const zero = Math.round(Math.abs(v) * 10) === 0;
  return { value: zero ? shown : `${v > 0 ? '+' : '−'}${shown}`, label: 'a week to make it' };
}

export type Tone = 'ok' | 'behind' | 'stale' | 'fail';

/** The short word on the right of a goal's row. */
export function paceWord(goal: Pick<Goal, 'kind' | 'unit' | 'progress'>): {
  text: string;
  tone: Tone;
} {
  const p = goal.progress;
  if (goal.kind === 'binary') {
    if (p.pace === 'passed') return { text: 'passed', tone: 'ok' };
    if (p.pace === 'failed') return { text: 'not passed', tone: 'fail' };
    const n = p.days_left;
    return {
      text: `${n} ${n === 1 ? 'day' : 'days'} left`,
      tone: p.pace === 'behind' ? 'behind' : 'ok',
    };
  }
  if (goal.kind === 'number') {
    if (p.stale) return { text: 'not updated', tone: 'stale' };
    if (p.pace === 'on_track') return { text: 'on track', tone: 'ok' };
    return { text: `behind ${formatValue(Math.abs(p.gap ?? 0), goal.unit)}`, tone: 'behind' };
  }
  if (goal.kind === 'count') {
    if (p.reached) return { text: 'reached', tone: 'ok' };
    if (p.pace === 'on_track') return { text: 'on track', tone: 'ok' };
    // The server calls it behind when done < floor(expected) (Progress.hpp).
    const behind = Math.max(1, Math.floor((p.expected ?? 0) + 1e-9) - (p.done ?? 0));
    return { text: `behind ${behind}`, tone: 'behind' };
  }
  return p.pace === 'on_track'
    ? { text: 'on track', tone: 'ok' }
    : { text: 'behind', tone: 'behind' };
}

/** The cells of a count goal's shelf: done ones, the next one, the rest. */
export function shelf(target: number, done: number): ('done' | 'next' | 'empty')[] {
  return Array.from({ length: target }, (_, i) =>
    i < done ? 'done' : i === done ? 'next' : 'empty',
  );
}

/**
 * The points of a number goal's chart, oldest first. The line starts at the
 * start date; a check-in dated on that day is the value there, as the server's
 * progress takes it as current (Progress.hpp), not start_value.
 * @p checkins come newest first, as the API lists them.
 */
export function chartPoints(
  startDate: string,
  startValue: number | null | undefined,
  checkins: Pick<GoalCheckin, 'date' | 'value'>[],
): [string, number][] {
  const asc = checkins.slice().reverse();
  const onStart = asc.filter((c) => c.date === startDate).pop();
  return [
    [startDate, onStart ? onStart.value : (startValue ?? 0)],
    ...asc.filter((c) => c.date !== startDate).map((c): [string, number] => [c.date, c.value]),
  ];
}
