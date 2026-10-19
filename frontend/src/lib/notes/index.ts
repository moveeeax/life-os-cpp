import { formatDay, todayLocal } from '@/lib/dates';

export type * from './types';
export * from './draft';
export * from './markdown';

/** "otel, tracing , otel" → ["otel", "tracing"]: trimmed, no empties, no repeats. */
export function tagsFrom(text: string): string[] {
  const out: string[] = [];
  for (const t of text.split(',')) {
    const tag = t.trim();
    if (tag && !out.includes(tag)) out.push(tag);
  }
  return out;
}

/**
 * The month a note was written in, for the list groups: "October 2026".
 * `created_at` is an instant, so it is read in the browser's zone (a note
 * written at 03:00 in UTC+5 is not yesterday's); @p timeZone is for tests.
 */
export function monthOf(iso: string, timeZone?: string): string {
  return new Date(iso).toLocaleDateString('en-GB', { month: 'long', year: 'numeric', timeZone });
}

/** The day a note was written, for its row: "Oct 12"; in the browser's zone, as monthOf. */
export function dayOf(iso: string, timeZone?: string): string {
  return new Date(iso).toLocaleDateString('en-US', { month: 'short', day: 'numeric', timeZone });
}

/**
 * The day a note's row shows and the month it is grouped in: the day it is
 * about when set (the list sorts by it), else the day it was written.
 */
export function noteDay(
  n: { date?: string | null; created_at: string },
  timeZone?: string,
): { day: string; month: string } {
  if (n.date) {
    return {
      day: formatDay(n.date, { yearUnless: todayLocal(timeZone) }),
      month: monthOf(`${n.date}T00:00:00Z`, 'UTC'),
    };
  }
  return { day: dayOf(n.created_at, timeZone), month: monthOf(n.created_at, timeZone) };
}
