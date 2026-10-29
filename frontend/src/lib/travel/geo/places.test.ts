import { describe, expect, it } from 'vitest';

import type { TravelTrip } from '../types';
import { cityIndex } from '../year';

import { placeDays, placesOf } from './places';

const TODAY = '2026-10-29';
const trip = (
  id: string,
  name: string,
  start_date: string,
  end_date: string,
  countries: string[],
) => ({ id, name, start_date, end_date, countries }) as TravelTrip;

const world = {
  cities: cityIndex(
    ['Tashkent\tUZ\t41.26\t69.22\tc', 'Almaty\tKZ\t43.25\t76.92', 'Phuket\tTH\t7.89\t98.4'].join(
      '\n',
    ),
  ),
  centre: new Map<string, [number, number]>([['VN', [790, 220]]]),
  project: (lon: number, lat: number): [number, number] => [lon, lat],
};

describe('placesOf', () => {
  it('puts a trip on its city, two trips to one city on one point', () => {
    const places = placesOf(
      world,
      [
        trip('a', 'Almaty (May 2026)', '2026-05-24', '2026-06-06', ['KZ']),
        trip('b', 'Almaty (Oct 2026)', '2026-10-10', '2026-10-24', ['KZ']),
        trip('c', 'Phuket', '2026-09-12', '2026-10-10', ['TH']),
      ],
      TODAY,
      2026,
    );
    expect(places.map((p) => [p.name, p.done, p.trips])).toEqual([
      ['Phuket', 28, ['c']],
      ['Almaty', 27, ['a', 'b']],
    ]);
    expect(places[1].x).toBe(76.92);
  });

  it('falls back to the country for a name that names no known city', () => {
    const [p] = placesOf(
      world,
      [trip('d', 'Mekong delta', '2026-08-01', '2026-08-05', ['VN'])],
      TODAY,
      2026,
    );
    expect([p.name, p.x, p.y]).toEqual(['Vietnam', 790, 220]);
  });

  it('keeps the plan of the trip going on and of one ahead', () => {
    const [now] = placesOf(
      world,
      [trip('e', 'Tashkent (Oct 2026)', '2026-10-24', '2026-11-20', ['UZ'])],
      TODAY,
      2026,
    );
    expect(placeDays(now)).toBe('5 of 27 d');
    const [later] = placesOf(
      world,
      [trip('f', 'Phuket', '2026-12-01', '2026-12-10', ['TH'])],
      TODAY,
      2026,
    );
    expect(placeDays(later)).toBe('9 d planned');
  });

  it('leaves out a trip with no city and no country', () => {
    expect(
      placesOf(world, [trip('g', 'Somewhere', '2026-01-01', '2026-01-02', [])], TODAY, 2026),
    ).toEqual([]);
  });
});
