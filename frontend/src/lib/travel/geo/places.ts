import { countryName } from '../index';
import type { TravelTrip } from '../types';
import { cityOfTrip, placeOfName, tripDays, type CityIndex } from '../year';

/** What placesOf needs of the loaded world (world.ts). */
export interface PlaceWorld {
  cities: CityIndex;
  centre: Map<string, [number, number]>;
  project: (lon: number, lat: number) => [number, number] | null;
}

export interface Place {
  key: string;
  name: string;
  country: string;
  x: number;
  y: number;
  done: number;
  plan: number;
  trips: string[];
}

/** One point per city of the year's trips (the country when the name names no known city). */
export function placesOf(
  world: PlaceWorld,
  trips: TravelTrip[],
  today: string,
  year: number,
): Place[] {
  const out = new Map<string, Place>();
  for (const t of trips) {
    const city = cityOfTrip(t, world.cities);
    const country = city?.country ?? t.countries[0];
    if (!country) continue;
    const xy = city ? world.project(city.lon, city.lat) : world.centre.get(country);
    if (!xy) continue;
    const key = city ? `${city.name}|${city.country}` : country;
    const d = tripDays(t, today, year);
    const p = out.get(key) ?? {
      key,
      // The trip's own spelling ("St Petersburg"), not the list's.
      name: city ? placeOfName(t.name) : countryName(country),
      country,
      x: xy[0],
      y: xy[1],
      done: 0,
      plan: 0,
      trips: [],
    };
    p.done += d.done;
    p.plan += d.plan;
    p.trips.push(t.id);
    out.set(key, p);
  }
  // The largest first, so a small point sits on top of a large neighbour.
  return [...out.values()].sort((a, b) => b.plan - a.plan);
}

export const placeDays = (p: Place) => {
  if (p.plan > p.done) return p.done ? `${p.done} of ${p.plan} d` : `${p.plan} d planned`;
  return p.done ? `${p.done} d` : 'day trip';
};
