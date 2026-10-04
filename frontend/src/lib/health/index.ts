// Pure helpers for the Health section: date ranges, per-day aggregation of
// raw samples, night selection, tile statistics and the pagination loop.
// No React, no fetch: everything here is unit-tested in health.test.ts.

import type { Page, SleepSession } from './types';

export interface DateRange {
  from: string; // YYYY-MM-DD, inclusive
  to: string; // YYYY-MM-DD, inclusive
}

export const PRESETS = [7, 30, 90] as const;
export type Preset = (typeof PRESETS)[number];
export const DEFAULT_PRESET: Preset = 30;

const DAY_MS = 86_400_000;

/** Earliest date a custom range may start on. */
export const MIN_DATE = '2020-01-01';
/** Longest custom range, in days. Raw samples are aggregated in the browser. */
export const MAX_RANGE_DAYS = 366;

function isIsoDate(s: string | null): s is string {
  if (!s || !/^\d{4}-\d{2}-\d{2}$/.test(s)) return false;
  const d = new Date(`${s}T00:00:00Z`);
  return !Number.isNaN(d.getTime()) && d.toISOString().slice(0, 10) === s;
}

function shiftDate(date: string, days: number): string {
  return new Date(Date.parse(`${date}T00:00:00Z`) + days * DAY_MS).toISOString().slice(0, 10);
}

/**
 * Calendar date of an instant in a zone `offsetMin` minutes ahead of UTC.
 * Defaults to the browser's zone at that instant.
 */
export function localDate(iso: string, offsetMin?: number): string {
  const d = new Date(iso);
  const off = offsetMin ?? -d.getTimezoneOffset();
  return new Date(d.getTime() + off * 60_000).toISOString().slice(0, 10);
}

/** Today's calendar date in the browser's zone. */
export function today(): string {
  return localDate(new Date().toISOString());
}

/** The last `days` days ending on `end`, both ends included. */
export function rangeForDays(days: number, end: string): DateRange {
  return { from: shiftDate(end, -(days - 1)), to: end };
}

/**
 * The range with a day added on each side. Timestamp routes filter by UTC
 * day, so a local-day range needs the neighbours to be complete at its edges.
 */
export function widenRange(range: DateRange): DateRange {
  return { from: shiftDate(range.from, -1), to: shiftDate(range.to, 1) };
}

/** Whether a calendar date lies inside the range. */
export function inRange(date: string, range: DateRange): boolean {
  return date >= range.from && date <= range.to;
}

/** Every date of the range in order. */
export function daysInRange(range: DateRange): string[] {
  const out: string[] = [];
  for (let d = range.from; d <= range.to; d = shiftDate(d, 1)) out.push(d);
  return out;
}

/**
 * Whether `from`..`to` is a usable custom range: real dates, in order, not
 * before MIN_DATE, not after `end` (today), and at most MAX_RANGE_DAYS long.
 * A date input reports a half-typed year such as 0002; that is rejected here.
 */
export function validCustomRange(from: string | null, to: string | null, end: string): boolean {
  if (!isIsoDate(from) || !isIsoDate(to)) return false;
  if (from > to || from < MIN_DATE || to > end) return false;
  const days = (Date.parse(`${to}T00:00:00Z`) - Date.parse(`${from}T00:00:00Z`)) / DAY_MS + 1;
  return days <= MAX_RANGE_DAYS;
}

export interface ParsedRange extends DateRange {
  preset: Preset | 'custom';
}

/**
 * Period from the URL query: `?days=7|30|90` or `?from=…&to=…`. Anything
 * malformed, reversed, outside the limits or unknown falls back to the
 * default preset.
 */
export function parseRange(params: URLSearchParams, end: string): ParsedRange {
  const from = params.get('from');
  const to = params.get('to');
  if (validCustomRange(from, to, end)) {
    return { preset: 'custom', from: from as string, to: to as string };
  }
  const days = Number(params.get('days'));
  const preset = (PRESETS as readonly number[]).includes(days) ? (days as Preset) : DEFAULT_PRESET;
  return { preset, ...rangeForDays(preset, end) };
}

export interface DayStat {
  date: string;
  min: number;
  avg: number;
  max: number;
  count: number;
}

/** Min, mean and max of raw samples per local day, in date order. */
export function dailyStats(
  samples: { timestamp: string; value: number }[],
  offsetMin?: number,
): DayStat[] {
  const byDay = new Map<string, { min: number; max: number; sum: number; count: number }>();
  for (const s of samples) {
    if (!Number.isFinite(s.value)) continue;
    const date = localDate(s.timestamp, offsetMin);
    const cur = byDay.get(date);
    if (cur) {
      cur.min = Math.min(cur.min, s.value);
      cur.max = Math.max(cur.max, s.value);
      cur.sum += s.value;
      cur.count += 1;
    } else {
      byDay.set(date, { min: s.value, max: s.value, sum: s.value, count: 1 });
    }
  }
  return [...byDay.entries()]
    .sort(([a], [b]) => (a < b ? -1 : 1))
    .map(([date, v]) => ({ date, min: v.min, avg: v.sum / v.count, max: v.max, count: v.count }));
}

export interface Night {
  /** Local date of waking up. */
  date: string;
  durationMinutes: number;
  asleepMinutes: number;
  deep: number;
  light: number;
  rem: number;
  awake: number;
  score: number | null;
}

/**
 * One night per local wake-up date: the longest session that has stages.
 * Naps and sessions without stages (short daytime sleep) are left out.
 */
export function nights(sessions: SleepSession[], offsetMin?: number): Night[] {
  const best = new Map<string, SleepSession>();
  for (const s of sessions) {
    if (s.is_nap || !s.stages || s.stages.length === 0) continue;
    const date = localDate(s.end_at, offsetMin);
    const cur = best.get(date);
    if (!cur || s.duration_minutes > cur.duration_minutes) best.set(date, s);
  }
  return [...best.entries()]
    .sort(([a], [b]) => (a < b ? -1 : 1))
    .map(([date, s]) => {
      const sum = { deep: 0, light: 0, rem: 0, awake: 0 };
      for (const st of s.stages) {
        if (st.stage in sum) sum[st.stage as keyof typeof sum] += st.minutes;
      }
      return {
        date,
        durationMinutes: s.duration_minutes,
        asleepMinutes: s.time_asleep_minutes,
        ...sum,
        score: s.sleep_score,
      };
    });
}

/** Mean of the present values and how many there were. */
export function mean(values: (number | null | undefined)[]): { value: number | null; n: number } {
  const present = values.filter((v): v is number => typeof v === 'number' && Number.isFinite(v));
  if (present.length === 0) return { value: null, n: 0 };
  return { value: present.reduce((a, b) => a + b, 0) / present.length, n: present.length };
}

/** Last weight in the range and its change from the first. Rows are in time order. */
export function weightChange(rows: { timestamp: string; weight_kg: number }[]): {
  last: number | null;
  delta: number | null;
  n: number;
} {
  if (rows.length === 0) return { last: null, delta: null, n: 0 };
  const last = rows[rows.length - 1].weight_kg;
  return { last, delta: rows.length > 1 ? last - rows[0].weight_kg : null, n: rows.length };
}

/**
 * Fetch every row of a paged route. Stops when `total` rows are in hand or
 * the server returns an empty page, whichever comes first.
 */
export async function fetchAllPages<T>(
  fetchPage: (limit: number, offset: number) => Promise<Page<T>>,
  limit = 10_000,
): Promise<T[]> {
  const rows: T[] = [];
  for (;;) {
    const page = await fetchPage(limit, rows.length);
    rows.push(...page.data);
    if (page.data.length === 0 || rows.length >= page.total) return rows;
  }
}

/**
 * One value per day of `days`: the last row of a day that has a value. A
 * later row without the value does not erase an earlier one. Days without a
 * row, and rows outside `days`, give null.
 */
export function alignToDays<T>(
  days: string[],
  rows: T[],
  date: (row: T) => string,
  value: (row: T) => number | null | undefined,
): (number | null)[] {
  const byDate = new Map<string, number>();
  for (const row of rows) {
    const v = value(row);
    if (typeof v === 'number' && Number.isFinite(v)) byDate.set(date(row), v);
  }
  return days.map((d) => byDate.get(d) ?? null);
}

/** A change with one decimal and a sign; a change that rounds to zero is `0.0`. */
export function formatDelta(delta: number): string {
  const r = Math.round(delta * 10) / 10;
  if (r === 0) return '0.0';
  return `${r > 0 ? '+' : ''}${r.toFixed(1)}`;
}

/** Minutes as `h:mm`. */
export function formatMinutes(minutes: number): string {
  const m = Math.round(minutes);
  return `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}`;
}
