import { describe, expect, it } from 'vitest';

import {
  countdownWords,
  counterLine,
  countryName,
  flightsWithoutCrossing,
  splitTrips,
} from './index';
import type { TravelCounter, TravelCrossing, TravelTrip, TravelTripItem } from './types';

describe('countryName', () => {
  it('names a country and falls back to the code', () => {
    expect(countryName('TH')).toBe('Thailand');
    expect(countryName('ZZ')).toBe('ZZ');
  });
});

describe('countdownWords', () => {
  it('reads the days to a date', () => {
    expect(countdownWords(0)).toBe('today');
    expect(countdownWords(1)).toBe('tomorrow');
    expect(countdownWords(19)).toBe('in 19 days');
  });
});

const counter = (over: Partial<TravelCounter>): TravelCounter => ({
  id: 'r1',
  name: 'Schengen',
  countries: ['SCHENGEN'],
  window_kind: 'rolling_days',
  window_days: 180,
  threshold_days: 90,
  threshold_kind: 'at_most',
  used: 34,
  left: 56,
  reached_on: null,
  reentry: null,
  source: '',
  ...over,
});

describe('counterLine', () => {
  it('shows used of the limit, or the days counted for an over rule', () => {
    expect(counterLine(counter({}))).toBe('34 of 90');
    expect(counterLine(counter({ threshold_kind: 'over', threshold_days: 183, used: 241 }))).toBe(
      '241 days',
    );
  });
});

describe('flightsWithoutCrossing', () => {
  const item = (id: string, kind: TravelTripItem['kind'], date: string): TravelTripItem => ({
    id,
    trip_id: 't',
    kind,
    date,
    time: null,
    title: id,
    reference: '',
    link: '',
    note: '',
  });
  const crossing = (date: string): TravelCrossing => ({
    id: date,
    date,
    from_country: 'KZ',
    to_country: 'TH',
    time: null,
    place: '',
    note: '',
  });
  it('lists the flights with no crossing on their date', () => {
    const items = [
      item('out', 'flight', '2026-11-01'),
      item('stay', 'stay', '2026-11-02'),
      item('back', 'flight', '2026-11-20'),
    ];
    expect(flightsWithoutCrossing(items, [crossing('2026-11-01')]).map((i) => i.id)).toEqual([
      'back',
    ]);
  });
});

describe('splitTrips', () => {
  const trip = (id: string, start: string, end: string) =>
    ({ id, start_date: start, end_date: end }) as TravelTrip;
  it('puts running and future trips first, soonest first, then the past, latest first', () => {
    const { coming, past } = splitTrips(
      [
        trip('bali', '2026-05-02', '2026-05-30'),
        trip('lisbon', '2027-02-10', '2027-02-17'),
        trip('th', '2026-11-01', '2026-11-20'),
        trip('now', '2026-10-10', '2026-10-15'),
      ],
      '2026-10-13',
    );
    expect(coming.map((t) => t.id)).toEqual(['now', 'th', 'lisbon']);
    expect(past.map((t) => t.id)).toEqual(['bali']);
  });
});
