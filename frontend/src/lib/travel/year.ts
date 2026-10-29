import { daysBetween } from '@/lib/dates';

import type { TravelTrip } from './types';

type Span = Pick<TravelTrip, 'start_date' | 'end_date'>;

/** The years the trips touch, latest first; the current year leads when any trip exists. */
export function tripYears(trips: Span[], today: string): number[] {
  const years = new Set<number>();
  for (const t of trips) {
    const a = Number(t.start_date.slice(0, 4));
    const b = Number(t.end_date.slice(0, 4));
    for (let y = a; y <= b; y++) years.add(y);
  }
  if (years.size > 0) years.add(Number(today.slice(0, 4)));
  return [...years].sort((a, b) => b - a);
}

/** The trips that overlap a year, earliest first. */
export function tripsOfYear<T extends Span>(trips: T[], year: number): T[] {
  const from = `${year}-01-01`;
  const to = `${year}-12-31`;
  return trips
    .filter((t) => t.start_date <= to && t.end_date >= from)
    .sort((a, b) => a.start_date.localeCompare(b.start_date));
}

export interface TripDays {
  /** Days lived so far: all of them for a finished trip, none for a future one. */
  done: number;
  /** Days from the start to the end as planned. */
  plan: number;
  live: boolean;
  future: boolean;
}

/** Nights of a trip within a year (a trip into the next year counts there too). */
export function tripDays(t: Span, today: string, year?: number): TripDays {
  const start = year && t.start_date < `${year}-01-01` ? `${year}-01-01` : t.start_date;
  const end = year && t.end_date > `${year}-12-31` ? `${year + 1}-01-01` : t.end_date;
  const plan = Math.max(daysBetween(start, end), 0);
  const live = t.start_date <= today && t.end_date >= today;
  const future = t.start_date > today;
  const done = future ? 0 : live ? Math.min(Math.max(daysBetween(start, today), 0), plan) : plan;
  return { done, plan, live, future };
}

/** "7 trips · 153 days away": the year's finished trips and their days in it. */
export function yearSummary(trips: Span[], today: string, year: number) {
  const done = tripsOfYear(trips, year).filter((t) => t.end_date < today);
  return {
    trips: done.length,
    days: done.reduce((s, t) => s + tripDays(t, today, year).plan, 0),
  };
}

/** Where a date sits in its year, 0 to 100, clipped to the year. */
export function yearPct(iso: string, year: number): number {
  const y0 = Date.UTC(year, 0, 1);
  const span = Date.UTC(year + 1, 0, 1) - y0;
  const v = ((Date.parse(`${iso}T00:00:00Z`) - y0) / span) * 100;
  return Math.min(100, Math.max(0, v));
}

/** "5 of 27 d", "14 d", "day trip", "in 12 d" for a trip of the year. */
export function daysWords(d: TripDays): string {
  if (d.live) return `${d.done} of ${d.plan} d`;
  if (d.plan === 0) return 'day trip';
  return `${d.plan} d`;
}

/** A place name folded for matching: lower case, no accents, "St" as "saint". */
export function foldPlace(s: string): string {
  return s
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .toLowerCase()
    .replace(/\bst\.?\s+/g, 'saint ')
    .replace(/\bste\.?\s+/g, 'sainte ')
    .replace(/[^a-z0-9]+/g, ' ')
    .trim();
}

/** The place a trip's name starts with: "Tashkent (Oct 2026)" → "Tashkent", "Bali, Ubud" → "Bali". */
export function placeOfName(name: string): string {
  return name
    .replace(/\([^)]*\)/g, ' ')
    .split(/[,/–—|]| - /)[0]
    .trim();
}

export interface City {
  name: string;
  country: string;
  lat: number;
  lon: number;
  capital: boolean;
}

export interface CityIndex {
  byName: Map<string, City[]>;
  capitals: Map<string, City>;
}

/** The bundled city list (name, country, lat, lon, "c" for a capital per line, largest first). */
export function cityIndex(tsv: string): CityIndex {
  const byName = new Map<string, City[]>();
  const capitals = new Map<string, City>();
  for (const line of tsv.split('\n')) {
    if (!line) continue;
    const [name, country, lat, lon, cap] = line.split('\t');
    const city: City = { name, country, lat: Number(lat), lon: Number(lon), capital: cap === 'c' };
    const key = foldPlace(name);
    const list = byName.get(key);
    if (list) list.push(city);
    else byName.set(key, [city]);
    if (city.capital && !capitals.has(country)) capitals.set(country, city);
  }
  return { byName, capitals };
}

/** The city a trip's name names, in one of its countries when it has any; the largest match wins. */
export function cityOfTrip(
  trip: Pick<TravelTrip, 'name' | 'countries'>,
  index: CityIndex,
): City | null {
  const list = index.byName.get(foldPlace(placeOfName(trip.name)));
  if (!list) return null;
  if (trip.countries.length === 0) return list[0];
  return list.find((c) => trip.countries.includes(c.country)) ?? null;
}
