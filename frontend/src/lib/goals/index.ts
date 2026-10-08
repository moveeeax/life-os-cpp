// Goals on the page: the kind labels, the pace word of a row, values with
// their unit and the shelf of a count goal.
export * from './types';
import type { Goal, GoalKind } from './types';

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

export type Tone = 'ok' | 'behind' | 'stale' | 'fail';

/** The short word on the right of a goal's row. */
export function paceWord(goal: Goal): { text: string; tone: Tone } {
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
    return { text: `behind ${Math.ceil((p.expected ?? 0) - (p.done ?? 0))}`, tone: 'behind' };
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
