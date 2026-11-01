import { describe, expect, it } from 'vitest';

import { arrival, flightsToday, hourIn, minutesUntil, modeNeeds, modeOf } from './modes';

describe('modeNeeds', () => {
  const trip = (days_to: number) => ({ start_date: '2026-11-01', days_to });

  it('reads nothing pinned off or to a time of day, with travel off, or before the digest', () => {
    const none = { trips: false, crossings: false };
    expect(modeNeeds('off', true, true, trip(0), false, false)).toEqual(none);
    expect(modeNeeds('morning', true, true, trip(0), false, false)).toEqual(none);
    expect(modeNeeds('evening', true, true, trip(0), false, false)).toEqual(none);
    expect(modeNeeds(null, false, true, trip(0), false, false)).toEqual(none);
    expect(modeNeeds(null, undefined, true, trip(0), false, false)).toEqual(none);
    expect(modeNeeds(null, true, false, undefined, false, false)).toEqual(none);
  });

  it('reads trips only while a trip runs or starts today', () => {
    expect(modeNeeds(null, true, true, trip(0), false, false).trips).toBe(true);
    expect(modeNeeds(null, true, true, trip(-3), false, false).trips).toBe(true);
    expect(modeNeeds(null, true, true, trip(5), false, false).trips).toBe(false);
    expect(modeNeeds(null, true, true, null, false, false).trips).toBe(false);
    expect(modeNeeds('arrive', true, true, trip(0), false, false).trips).toBe(false);
  });

  it('reads the newest crossings once no flight wins', () => {
    expect(modeNeeds(null, true, true, trip(5), false, false).crossings).toBe(true);
    expect(modeNeeds(null, true, true, trip(0), false, false).crossings).toBe(false);
    expect(modeNeeds(null, true, true, trip(0), false, true).crossings).toBe(true);
    expect(modeNeeds(null, true, true, trip(0), true, true).crossings).toBe(false);
    expect(modeNeeds('flight', true, true, trip(5), false, true).crossings).toBe(false);
    expect(modeNeeds('arrive', true, true, null, false, false).crossings).toBe(true);
  });
});

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
