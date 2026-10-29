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

/** Minutes from @p now to a local "HH:MM" of today, negative when past. */
export function minutesUntil(time: string | null | undefined, now: Date): number | null {
  if (!time || !/^\d{2}:\d{2}$/.test(time)) return null;
  const [h, m] = time.split(':').map(Number);
  return h * 60 + m - (now.getHours() * 60 + now.getMinutes());
}
