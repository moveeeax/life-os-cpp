// Calendar days as the API speaks them: "YYYY-MM-DD", no time, no zone.
// Day maths runs in UTC on those strings, so a clock change never moves a day.

const DAY_RE = /^(\d{4})-(\d{2})-(\d{2})$/;
const DAY_MS = 86_400_000;
const pad = (n: number) => String(n).padStart(2, '0');

/** The UTC midnight of a real "YYYY-MM-DD", or null. */
function utcOf(date: string): Date | null {
  const m = DAY_RE.exec(date);
  if (!m) return null;
  const [y, mo, d] = [Number(m[1]), Number(m[2]), Number(m[3])];
  const t = new Date(Date.UTC(y, mo - 1, d));
  return t.getUTCFullYear() === y && t.getUTCMonth() === mo - 1 && t.getUTCDate() === d ? t : null;
}

/** Whether @p date is a real "YYYY-MM-DD". */
export const isDay = (date: string): boolean => utcOf(date) !== null;

/**
 * Today's "YYYY-MM-DD": in the zone @p tz when given (an IANA name), else in
 * the browser's own zone.
 */
export function todayLocal(tz?: string, now: Date = new Date()): string {
  if (!tz) return `${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())}`;
  const parts = new Intl.DateTimeFormat('en-CA', {
    timeZone: tz,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  }).formatToParts(now);
  const get = (t: string) => parts.find((p) => p.type === t)?.value ?? '';
  return `${get('year')}-${get('month')}-${get('day')}`;
}

/** The day @p by days from @p date; a broken @p date counts from today. */
export function shiftDays(date: string, by: number): string {
  const t = utcOf(date) ?? utcOf(todayLocal())!;
  return new Date(t.getTime() + by * DAY_MS).toISOString().slice(0, 10);
}

/** The Monday of the week (Monday to Sunday) @p date is in; a broken @p date counts from today. */
export function mondayOf(date: string): string {
  const t = utcOf(date) ?? utcOf(todayLocal())!;
  return shiftDays(t.toISOString().slice(0, 10), -((t.getUTCDay() + 6) % 7));
}

/** Whole calendar days from @p from to @p to (negative when @p to is earlier); NaN for a broken day. */
export function daysBetween(from: string, to: string): number {
  const a = utcOf(from);
  const b = utcOf(to);
  return a && b ? Math.round((b.getTime() - a.getTime()) / DAY_MS) : NaN;
}

export interface DayFormat {
  /** Lead with the short weekday ("Mon, Oct 5"). */
  weekday?: boolean;
  /** 'en-US' (default): "Oct 5"; 'en-GB': "5 Oct". */
  locale?: 'en-US' | 'en-GB';
  /** Add the year when @p date is not in the year of this day. */
  yearUnless?: string;
}

/**
 * A day for people: "Mon, Oct 5" by default, or as @p f asks. A broken day
 * comes back as it was.
 */
export function formatDay(date: string, f: DayFormat = { weekday: true }): string {
  const t = utcOf(date);
  if (!t) return date;
  return t.toLocaleDateString(f.locale ?? 'en-US', {
    ...(f.weekday ? { weekday: 'short' } : {}),
    month: 'short',
    day: 'numeric',
    ...(f.yearUnless && f.yearUnless.slice(0, 4) !== date.slice(0, 4) ? { year: 'numeric' } : {}),
    timeZone: 'UTC',
  });
}

/** An instant in the browser's date and time format. */
export function formatDateTime(iso: string): string {
  try {
    return new Date(iso).toLocaleString();
  } catch {
    return iso;
  }
}
