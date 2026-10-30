import { describe, expect, it } from 'vitest';

import { arrival, flightsToday, hourIn, minutesUntil, modeOf } from './modes';

describe('modes', () => {
  it('ModeOfEachSituation', () => {
    expect(modeOf(7, true, true, null)).toBe('flight');
    expect(modeOf(13, false, true, null)).toBe('arrive');
    expect(modeOf(8, false, false, null)).toBe('morning');
    expect(modeOf(21, false, false, null)).toBe('evening');
    expect(modeOf(14, false, false, null)).toBeNull();
    expect(modeOf(8, true, false, 'off')).toBeNull();
    expect(modeOf(14, false, false, 'evening')).toBe('evening');
  });

  it('finds today flights and a recent arrival', () => {
    const trip = {
      items: [
        { kind: 'flight', date: '2026-11-20', time: '08:20', title: 'TAS → ALA' },
        { kind: 'stay', date: '2026-11-20', time: null, title: 'Hotel' },
        { kind: 'flight', date: '2026-11-21', time: '06:00', title: 'Next' },
      ],
    } as never;
    expect(flightsToday(trip, '2026-11-20').map((f) => f.title)).toEqual(['TAS → ALA']);
    const crossings = [
      { date: '2026-10-24', time: '14:00', from_country: 'KZ', to_country: 'UZ' },
      { date: '2026-10-10', time: null, from_country: 'TH', to_country: 'KZ' },
    ] as never;
    expect(arrival(crossings, '2026-10-26')?.to_country).toBe('UZ');
    expect(arrival(crossings, '2026-10-29')).toBeNull();
    expect(minutesUntil('06:05', new Date('2026-11-20T05:10:00'))).toBe(55);
  });
});

describe('hourIn', () => {
  it('ModeInTheProfileZone: the hour of the profile zone, not the browser', () => {
    const at = new Date('2026-10-30T05:30:00Z');
    expect(hourIn(at, 'Asia/Tashkent')).toBe(10);
    expect(hourIn(at, 'America/New_York')).toBe(1);
    expect(hourIn(new Date('2026-10-30T19:00:00Z'), 'Asia/Tashkent')).toBe(0);
  });
});
