import type { components } from '@/lib/api/schema.gen';

export type Widget = components['schemas']['NowWidget'];
export const WIDGETS_MAX = 6;

/** The fixed widgets and their names; a series is named by its catalog entry. */
export const FIXED: Record<Exclude<Widget['kind'], 'series'>, string> = {
  calendar_week: 'Calendar week',
  habits: 'Habits today',
  spend: 'Spending this month',
  telegram: 'Telegram',
  trip: 'Next trip',
};

export const widgetKey = (w: Widget) => (w.kind === 'series' ? `series:${w.source}` : w.kind);

/**
 * The words under a sparkline: the last value against the average of the
 * days before it ("4 above usual", "as usual"); empty with too few days.
 */
export function trendWords(values: (number | null)[], unit: string): string {
  const v = values.filter((x): x is number => x !== null);
  if (v.length < 4) return '';
  const last = v[v.length - 1];
  const before = v.slice(0, -1);
  const avg = before.reduce((s, x) => s + x, 0) / before.length;
  const diff = last - avg;
  const scale = Math.max(Math.abs(avg), 1);
  if (Math.abs(diff) / scale < 0.05) return 'as usual';
  const n =
    Math.abs(diff) >= 10 ? Math.round(Math.abs(diff)) : Math.round(Math.abs(diff) * 10) / 10;
  return `${n}${unit ? ` ${unit}` : ''} ${diff > 0 ? 'above' : 'below'} usual`;
}

/** Points of a sparkline in a 100 x 34 box; nulls are skipped. */
export function sparkPoints(values: (number | null)[]): string {
  const pts = values
    .map((v, i) => ({ v, i }))
    .filter((p): p is { v: number; i: number } => p.v !== null);
  if (pts.length < 2) return '';
  const min = Math.min(...pts.map((p) => p.v));
  const max = Math.max(...pts.map((p) => p.v));
  const n = values.length - 1 || 1;
  return pts
    .map(
      (p) =>
        `${((p.i / n) * 100).toFixed(1)},${(30 - ((p.v - min) / (max - min || 1)) * 26).toFixed(1)}`,
    )
    .join(' ');
}

/** Monday to Sunday of the week of @p date (YYYY-MM-DD). */
export function weekRange(date: string): { from: string; to: string; days: string[] } {
  const d = new Date(`${date}T12:00:00Z`);
  const dow = (d.getUTCDay() + 6) % 7;
  d.setUTCDate(d.getUTCDate() - dow);
  const days: string[] = [];
  for (let i = 0; i < 7; i++) {
    days.push(d.toISOString().slice(0, 10));
    d.setUTCDate(d.getUTCDate() + 1);
  }
  return { from: days[0], to: days[6], days };
}
