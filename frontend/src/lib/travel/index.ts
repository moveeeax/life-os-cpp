import type { TravelCounter, TravelCrossing, TravelTrip, TravelTripItem } from './types';

export type * from './types';

let names: Intl.DisplayNames | null = null;

/** "TH" → "Thailand"; an unknown code stays as it is. */
export function countryName(code: string): string {
  try {
    names ??= new Intl.DisplayNames(['en'], { type: 'region', fallback: 'code' });
    const name = names.of(code);
    // CLDR names reserved codes such as ZZ "Unknown Region": show the code instead.
    return name && name !== 'Unknown Region' ? name : code;
  } catch {
    return code;
  }
}

/** Days to a date as words: "today", "tomorrow", "in 19 days". */
export function countdownWords(days: number): string {
  if (days <= 0) return 'today';
  if (days === 1) return 'tomorrow';
  return `in ${days} days`;
}

/** "34 of 90" for a limit, "241 days" for a count that becomes something over a threshold. */
export function counterLine(c: TravelCounter): string {
  return c.threshold_kind === 'at_most' ? `${c.used} of ${c.threshold_days}` : `${c.used} days`;
}

/** The flights of a plan with no recorded crossing on their date (a flight needing none left out). */
export function flightsWithoutCrossing(
  items: TravelTripItem[],
  crossings: TravelCrossing[],
): TravelTripItem[] {
  const days = new Set(crossings.map((c) => c.date));
  return items.filter((i) => i.kind === 'flight' && !i.no_crossing && !days.has(i.date));
}

/** Running and future trips soonest first; finished ones latest first. */
export function splitTrips<T extends Pick<TravelTrip, 'start_date' | 'end_date'>>(
  trips: T[],
  today: string,
) {
  const coming = trips
    .filter((t) => t.end_date >= today)
    .sort((a, b) => a.start_date.localeCompare(b.start_date));
  const past = trips
    .filter((t) => t.end_date < today)
    .sort((a, b) => b.start_date.localeCompare(a.start_date));
  return { coming, past };
}

/** Whole days from @p today to @p date (both YYYY-MM-DD). */
export function daysBetween(today: string, date: string): number {
  return Math.round(
    (Date.parse(`${date}T00:00:00Z`) - Date.parse(`${today}T00:00:00Z`)) / 86_400_000,
  );
}

/** "Nov 1" for a day of this year, "Nov 1, 2025" otherwise. */
export function shortDay(iso: string, today: string): string {
  const d = new Date(`${iso}T00:00:00Z`);
  return d.toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    ...(iso.slice(0, 4) === today.slice(0, 4) ? {} : { year: 'numeric' }),
    timeZone: 'UTC',
  });
}

export function todayLocal(): string {
  const d = new Date();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${d.getFullYear()}-${m}-${day}`;
}

/**
 * The one country of today and the day of the last crossing into it (null if none
 * recorded); null on a crossing day or with no country at all.
 */
export function whereNow(
  crossings: TravelCrossing[],
  here: string[],
  today: string,
): { country: string; since: string | null } | null {
  if (here.length !== 1) return null;
  const into = crossings
    .filter((c) => c.date <= today && c.to_country === here[0])
    .reduce<string | null>((a, c) => (a && a > c.date ? a : c.date), null);
  return { country: here[0], since: into };
}
