import { daysBetween } from '@/lib/dates';
import type { TravelCrossing, TravelTrip, TravelTripItem } from '@/lib/travel';

export type Mode = 'flight' | 'arrive' | 'morning' | 'evening';

/** Pinned to one mode, "off", or empty for by itself (localStorage). */
export const MODE_KEY = 'now:mode';

/** Morning cards until this hour, evening cards from this one. */
export const MORNING_UNTIL = 10;
export const EVENING_FROM = 21;
/** A crossing this many days ago or fewer still counts as arriving. */
export const ARRIVE_DAYS = 2;

/** Today's flights of a trip, earliest first. */
export function flightsToday(
  trip: Pick<TravelTrip, 'items'> | undefined,
  date: string,
): TravelTripItem[] {
  return (trip?.items ?? [])
    .filter((i) => i.kind === 'flight' && i.date === date)
    .sort((a, b) => (a.time ?? '').localeCompare(b.time ?? ''));
}

/** The latest crossing into a country within ARRIVE_DAYS of @p date, if any. */
export function arrival(
  crossings: TravelCrossing[] | undefined,
  date: string,
): TravelCrossing | null {
  const recent = (crossings ?? [])
    .filter((c) => c.date <= date && daysBetween(c.date, date) <= ARRIVE_DAYS)
    .sort((a, b) => b.date.localeCompare(a.date) || (b.time ?? '').localeCompare(a.time ?? ''));
  return recent[0] ?? null;
}

/** Which mode Now is in: a flight today first, then arriving, then the time of day. */
export function modeOf(
  hour: number,
  hasFlight: boolean,
  arrived: boolean,
  pinned: string | null,
): Mode | null {
  if (pinned === 'off') return null;
  if (pinned === 'flight' || pinned === 'arrive' || pinned === 'morning' || pinned === 'evening')
    return pinned;
  if (hasFlight) return 'flight';
  if (arrived) return 'arrive';
  if (hour < MORNING_UNTIL) return 'morning';
  if (hour >= EVENING_FROM) return 'evening';
  return null;
}

/** The running or next trip as the /today digest carries it (null with none). */
export type NextTrip = { start_date: string; days_to: number } | null | undefined;

/**
 * What Now has to read before it can tell its mode, so nothing is asked that
 * the mode will not show. Trips are read only while a trip runs or starts
 * today (a flight sits on its first or last day); the newest crossings only
 * while an arrival can still win (no flight pinned or found, no time-of-day
 * pin). Travel off, or the digest not here yet, reads neither.
 */
export function modeNeeds(
  pinned: string | null,
  travelOn: boolean | undefined,
  todayLoaded: boolean,
  next: NextTrip,
  hasFlight: boolean,
  tripsSettled: boolean,
): { trips: boolean; crossings: boolean } {
  if (pinned === 'off' || pinned === 'morning' || pinned === 'evening')
    return { trips: false, crossings: false };
  if (travelOn !== true || !todayLoaded) return { trips: false, crossings: false };
  const trips = pinned !== 'arrive' && next != null && next.days_to <= 0;
  const crossings = pinned !== 'flight' && !hasFlight && (!trips || tripsSettled);
  return { trips, crossings };
}

/** Minutes from @p now to a local "HH:MM" of today, negative when past. */
export function minutesUntil(time: string | null | undefined, now: Date): number | null {
  if (!time || !/^\d{2}:\d{2}$/.test(time)) return null;
  const [h, m] = time.split(':').map(Number);
  return h * 60 + m - (now.getHours() * 60 + now.getMinutes());
}

/** The hour (0-23) of @p now on the clock of @p tz, the profile's zone rather than the browser's. */
export function hourIn(now: Date, tz: string): number {
  const h = new Intl.DateTimeFormat('en-GB', {
    hour: '2-digit',
    hourCycle: 'h23',
    timeZone: tz,
  }).format(now);
  return Number(h) % 24;
}
