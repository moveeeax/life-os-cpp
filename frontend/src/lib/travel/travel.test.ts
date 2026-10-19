import { describe, expect, it } from 'vitest';

import {
  countdownWords,
  counterLine,
  countryName,
  flightsWithoutCrossing,
  documentWarningLine,
  headlineTrip,
  itemSub,
  splitTrips,
  telLink,
  whereNow,
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
    no_crossing: false,
    end_date: null,
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
  it('leaves out a flight marked as needing no crossing (domestic, or not mine)', () => {
    const domestic = { ...item('ctu', 'flight', '2026-11-05'), no_crossing: true };
    expect(flightsWithoutCrossing([domestic], [])).toEqual([]);
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

describe('whereNow', () => {
  const x = (date: string, from: string, to: string) =>
    ({
      id: date + to,
      date,
      from_country: from,
      to_country: to,
      time: null,
      place: '',
      note: '',
    }) as TravelCrossing;
  // Newest first, as the list comes.
  const list = [
    x('2026-11-01', 'KZ', 'TH'),
    x('2026-09-14', 'DE', 'KZ'),
    x('2026-08-24', 'GE', 'DE'),
  ];
  it('takes the country of today, not a crossing still to come', () => {
    expect(whereNow(list, ['KZ'], '2026-10-13')).toEqual({ country: 'KZ', since: '2026-09-14' });
  });
  it('has no since on a crossing day or without a crossing into it', () => {
    expect(whereNow(list, ['DE', 'KZ'], '2026-09-14')).toBeNull();
    expect(whereNow([], ['KZ'], '2026-10-13')).toEqual({ country: 'KZ', since: null });
    expect(whereNow([], [], '2026-10-13')).toBeNull();
  });
});

describe('documentWarningLine', () => {
  const w = { document_id: 'd', holder: 'Maksim', kind: 'passport', valid_until: '2027-05-01' };
  it('names the passport rule', () => {
    expect(documentWarningLine({ ...w, reason: 'passport_6_months' }, '2026-10-14')).toMatch(
      /^Maksim's passport is valid until .*May.*2027, less than six months after the trip$/,
    );
  });
  it('names a visa that ends during the trip', () => {
    expect(
      documentWarningLine(
        { ...w, kind: 'visa', valid_until: '2026-11-10', reason: 'expires_during_trip' },
        '2026-10-14',
      ),
    ).toMatch(/^Maksim's visa ends on .*Nov.*10, before the trip does$/);
  });
});

describe('headlineTrip', () => {
  const trip = (id: string, start: string, end: string) =>
    ({ id, start_date: start, end_date: end }) as TravelTrip;
  it('takes the running trip over the next one ahead', () => {
    const today = '2026-05-10';
    const { coming } = splitTrips(
      [trip('lisbon', '2026-06-01', '2026-06-07'), trip('bali', '2026-05-02', '2026-05-30')],
      today,
    );
    expect(headlineTrip(coming, today)?.id).toBe('bali');
    const later = splitTrips([trip('lisbon', '2026-06-01', '2026-06-07')], '2026-05-31').coming;
    expect(headlineTrip(later, '2026-05-31')?.id).toBe('lisbon');
    expect(headlineTrip([], today)).toBeUndefined();
  });
});

describe('itemSub', () => {
  const base: TravelTripItem = {
    id: 'i',
    trip_id: 't',
    kind: 'stay',
    date: '2026-11-02',
    time: null,
    title: 'Lanta Sand',
    reference: '',
    link: '',
    note: '',
    no_crossing: false,
    end_date: '2026-11-09',
  };
  it('a stay says until when and how many nights', () => {
    expect(itemSub(base, '2026-10-19')).toBe('stay · until Nov 9 · 7 nights');
    expect(itemSub({ ...base, end_date: '2026-11-03' }, '2026-10-19')).toBe(
      'stay · until Nov 3 · 1 night',
    );
    expect(itemSub({ ...base, end_date: null }, '2026-10-19')).toBe('stay');
    expect(itemSub({ ...base, kind: 'flight', time: '23:40', end_date: null }, '2026-10-19')).toBe(
      'flight · 23:40',
    );
  });
});

describe('telLink', () => {
  it('keeps the digits and a leading plus', () => {
    expect(telLink('+66 (81) 234-5678')).toBe('tel:+66812345678');
    expect(telLink('ask at the desk')).toBeNull();
  });
});
