import { describe, expect, it } from 'vitest';

import {
  cityIndex,
  cityOfTrip,
  daysWords,
  foldPlace,
  placeOfName,
  tripDays,
  tripsOfYear,
  tripYears,
  yearPct,
  yearSummary,
} from './year';

const TODAY = '2026-10-29';
const t = (name: string, start_date: string, end_date: string, countries: string[] = []) => ({
  name,
  start_date,
  end_date,
  countries,
});

const TRIPS = [
  t('Tashkent (Oct 2026)', '2026-10-24', '2026-11-20', ['UZ']),
  t('St Petersburg (Oct 2026)', '2026-10-17', '2026-10-17', ['RU']),
  t('Almaty (Oct 2026)', '2026-10-10', '2026-10-24', ['KZ']),
  t('Phuket', '2026-09-12', '2026-10-10', ['TH']),
  t('Ho Chi Minh City', '2026-08-01', '2026-09-12', ['VN']),
  t('Sanya', '2026-07-03', '2026-08-01', ['CN']),
  t('Istanbul', '2026-06-06', '2026-07-03', ['TR']),
  t('Almaty (May 2026)', '2026-05-24', '2026-06-06', ['KZ']),
  t('Bali', '2025-12-20', '2026-01-05', ['ID']),
];

describe('travel year', () => {
  it('lists the touched years latest first', () => {
    expect(tripYears(TRIPS, TODAY)).toEqual([2026, 2025]);
    expect(tripYears([], TODAY)).toEqual([]);
  });

  it('takes the trips overlapping a year, earliest first', () => {
    const names = tripsOfYear(TRIPS, 2025).map((x) => x.name);
    expect(names).toEqual(['Bali']);
    expect(tripsOfYear(TRIPS, 2026)[0].name).toBe('Bali');
  });

  it('counts nights, the running trip so far, and clips to the year', () => {
    expect(tripDays(TRIPS[0], TODAY)).toEqual({ done: 5, plan: 27, live: true, future: false });
    expect(tripDays(TRIPS[1], TODAY).plan).toBe(0);
    expect(tripDays(TRIPS[2], TODAY).plan).toBe(14);
    expect(tripDays(TRIPS[8], TODAY, 2025).plan).toBe(12);
    expect(tripDays(TRIPS[8], TODAY, 2026).plan).toBe(4);
    expect(tripDays(t('Later', '2026-12-01', '2026-12-10'), TODAY)).toEqual({
      done: 0,
      plan: 9,
      live: false,
      future: true,
    });
  });

  it('sums the finished trips of the year', () => {
    // 0 + 14 + 28 + 42 + 29 + 27 + 13 + 4 (Bali in January)
    expect(yearSummary(TRIPS, TODAY, 2026)).toEqual({ trips: 8, days: 157 });
  });

  it('places dates on the year and words the days', () => {
    expect(yearPct('2026-01-01', 2026)).toBe(0);
    expect(yearPct('2025-06-01', 2026)).toBe(0);
    expect(Math.round(yearPct('2026-07-02', 2026))).toBe(50);
    expect(daysWords(tripDays(TRIPS[0], TODAY))).toBe('5 of 27 d');
    expect(daysWords(tripDays(TRIPS[1], TODAY))).toBe('day trip');
    expect(daysWords(tripDays(TRIPS[3], TODAY))).toBe('28 d');
  });

  it('reads the place from a trip name', () => {
    expect(placeOfName('Tashkent (Oct 2026)')).toBe('Tashkent');
    expect(placeOfName('Bali, Ubud')).toBe('Bali');
    expect(placeOfName('Tbilisi – Batumi')).toBe('Tbilisi');
    expect(foldPlace('St Petersburg')).toBe('saint petersburg');
    expect(foldPlace('Türkiye')).toBe('turkiye');
  });

  it('finds the city in the trip countries, the largest first', () => {
    const index = cityIndex(
      [
        'Saint Petersburg\tRU\t59.94\t30.31',
        'Moscow\tRU\t55.75\t37.62\tc',
        'Saint Petersburg\tUS\t27.77\t-82.68',
        'Almaty\tKZ\t43.25\t76.92',
      ].join('\n'),
    );
    expect(cityOfTrip(TRIPS[1], index)?.lat).toBe(59.94);
    expect(cityOfTrip(t('St Petersburg', '2026-01-01', '2026-01-02', ['US']), index)?.lat).toBe(
      27.77,
    );
    expect(cityOfTrip(TRIPS[3], index)).toBeNull();
    expect(cityOfTrip(t('Almaty', '2026-01-01', '2026-01-02', ['RU']), index)).toBeNull();
    expect(index.capitals.get('RU')?.name).toBe('Moscow');
  });
});
