// Goals on the page: the kind labels, the pace word of a row, values with
// their unit and the shelf of a count goal.
export * from './types';
import type { Goal, GoalDetail, GoalKind, GoalPoint } from './types';

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

/** A point of the chart: the start of the line, a day of the source, or a check-in. */
export interface ChartPoint {
  date: string;
  value: number;
  from: 'start' | GoalPoint['from'];
}

/**
 * The points of a number goal's chart, oldest first: the source days merged
 * with the check-ins (the detail's `points`), or the check-ins alone when it
 * has none. The line starts at the start date; a point dated on that day is
 * the value there, as the server's progress takes it as current (Progress.hpp),
 * not start_value.
 */
export function goalChart(
  g: Pick<GoalDetail, 'start_date' | 'start_value' | 'checkins'> & { points?: GoalPoint[] },
): ChartPoint[] {
  const points: GoalPoint[] = g.points?.length
    ? g.points
    : g.checkins
        .slice()
        .reverse()
        .map((c) => ({ date: c.date, value: c.value, from: 'checkin' as const }));
  const onStart = points.filter((p) => p.date === g.start_date).pop();
  const head: ChartPoint = onStart ?? {
    date: g.start_date,
    value: g.start_value ?? 0,
    from: 'start',
  };
  return [head, ...points.filter((p) => p.date > g.start_date)];
}

/** The date of the newest day the source gave, or null. */
export function lastSourceDate(points: readonly Pick<GoalPoint, 'date' | 'from'>[]): string | null {
  let last: string | null = null;
  for (const p of points)
    if (p.from === 'source' && (last === null || p.date > last)) last = p.date;
  return last;
}

export interface AxisLabel {
  x: number;
  text: string;
  anchor: 'start' | 'middle' | 'end';
}

/**
 * The labels under the goal chart: the start date running right from the left
 * edge, the due date running left from the right one, "today" centred on its
 * line. A date label that would overlap "today" (each about @p labelWidth wide)
 * is dropped.
 */
export function axisLabels(a: {
  start: { x: number; text: string };
  end: { x: number; text: string };
  today: number | null;
  labelWidth?: number;
}): AxisLabel[] {
  const w = a.labelWidth ?? 44;
  const clear = (from: number, to: number) =>
    a.today === null || to <= a.today - w / 2 || from >= a.today + w / 2;
  const out: AxisLabel[] = [];
  if (clear(a.start.x, a.start.x + w)) {
    out.push({ x: a.start.x, text: a.start.text, anchor: 'start' });
  }
  if (clear(a.end.x - w, a.end.x)) out.push({ x: a.end.x, text: a.end.text, anchor: 'end' });
  if (a.today !== null) out.push({ x: a.today, text: 'today', anchor: 'middle' });
  return out;
}
