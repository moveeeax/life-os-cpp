import { describe, expect, it } from 'vitest';

import {
  dailyStats,
  daysInRange,
  fetchAllPages,
  inRange,
  localDate,
  mean,
  nights,
  parseRange,
  rangeForDays,
  weightChange,
  widenRange,
} from './index';
import type { SleepSession } from './types';

const session = (over: Partial<SleepSession>): SleepSession => ({
  sleep_id: 'x',
  start_at: '2026-09-30T17:53:00+00:00',
  end_at: '2026-10-01T02:53:00+00:00',
  duration_minutes: 540,
  time_asleep_minutes: 527,
  time_awake_minutes: 13,
  sleep_score: 80,
  sleep_score_source: 'daily_report',
  is_nap: false,
  timezone: 'UTC',
  stages: [
    { stage: 'light', minutes: 300 },
    { stage: 'deep', minutes: 46 },
    { stage: 'rem', minutes: 142 },
    { stage: 'awake', minutes: 13 },
    { stage: 'light', minutes: 39 },
  ],
  ...over,
});

describe('localDate', () => {
  it('moves a late-evening UTC instant to the next day in a zone ahead of UTC', () => {
    expect(localDate('2026-10-01T19:30:00+00:00', 420)).toBe('2026-10-02');
  });
  it('moves an early-morning UTC instant to the previous day in a zone behind UTC', () => {
    expect(localDate('2026-10-01T02:00:00+00:00', -300)).toBe('2026-09-30');
  });
  it('keeps the date at offset zero', () => {
    expect(localDate('2026-10-01T23:59:59+00:00', 0)).toBe('2026-10-01');
  });
});

describe('rangeForDays / daysInRange / parseRange', () => {
  it('counts the end day in the period', () => {
    expect(rangeForDays(7, '2026-10-05')).toEqual({ from: '2026-09-29', to: '2026-10-05' });
    expect(rangeForDays(1, '2026-10-05')).toEqual({ from: '2026-10-05', to: '2026-10-05' });
  });
  it('lists every date across a month boundary', () => {
    expect(daysInRange({ from: '2026-09-29', to: '2026-10-02' })).toEqual([
      '2026-09-29',
      '2026-09-30',
      '2026-10-01',
      '2026-10-02',
    ]);
  });
  it('widens by a day on each side and tests membership', () => {
    const r = { from: '2026-10-01', to: '2026-10-05' };
    expect(widenRange(r)).toEqual({ from: '2026-09-30', to: '2026-10-06' });
    expect(inRange('2026-10-01', r)).toBe(true);
    expect(inRange('2026-10-05', r)).toBe(true);
    expect(inRange('2026-09-30', r)).toBe(false);
    expect(inRange('2026-10-06', r)).toBe(false);
  });
  it('defaults to 30 days', () => {
    expect(parseRange(new URLSearchParams(''), '2026-10-05')).toEqual({
      preset: 30,
      from: '2026-09-06',
      to: '2026-10-05',
    });
  });
  it('reads a preset', () => {
    expect(parseRange(new URLSearchParams('days=7'), '2026-10-05').preset).toBe(7);
  });
  it('reads a custom range', () => {
    expect(parseRange(new URLSearchParams('from=2026-08-01&to=2026-08-10'), '2026-10-05')).toEqual({
      preset: 'custom',
      from: '2026-08-01',
      to: '2026-08-10',
    });
  });
  it('falls back to the default on a malformed, reversed or unknown value', () => {
    const dflt = parseRange(new URLSearchParams(''), '2026-10-05');
    expect(parseRange(new URLSearchParams('from=2026-13-40&to=x'), '2026-10-05')).toEqual(dflt);
    expect(parseRange(new URLSearchParams('from=2026-08-10&to=2026-08-01'), '2026-10-05')).toEqual(
      dflt,
    );
    expect(parseRange(new URLSearchParams('days=13'), '2026-10-05')).toEqual(dflt);
  });
});

describe('dailyStats', () => {
  it('groups samples by local day and reports min, mean, max and count', () => {
    const rows = [
      { timestamp: '2026-10-01T16:00:00+00:00', value: 60 },
      { timestamp: '2026-10-01T18:00:00+00:00', value: 80 }, // 01:00 next day at +07
      { timestamp: '2026-10-01T20:00:00+00:00', value: 100 },
    ];
    expect(dailyStats(rows, 420)).toEqual([
      { date: '2026-10-01', min: 60, avg: 60, max: 60, count: 1 },
      { date: '2026-10-02', min: 80, avg: 90, max: 100, count: 2 },
    ]);
  });
  it('returns nothing for no samples', () => {
    expect(dailyStats([], 0)).toEqual([]);
  });
  it('skips samples whose value is not a finite number', () => {
    const rows = [
      { timestamp: '2026-10-01T10:00:00+00:00', value: Number.NaN },
      { timestamp: '2026-10-01T11:00:00+00:00', value: 70 },
    ];
    expect(dailyStats(rows, 0)).toEqual([
      { date: '2026-10-01', min: 70, avg: 70, max: 70, count: 1 },
    ]);
  });
});

describe('nights', () => {
  it('sums stage minutes per stage and dates the night by local wake-up day', () => {
    expect(nights([session({})], 420)).toEqual([
      {
        date: '2026-10-01',
        durationMinutes: 540,
        asleepMinutes: 527,
        deep: 46,
        light: 339,
        rem: 142,
        awake: 13,
        score: 80,
      },
    ]);
  });
  it('keeps the longest session with stages when a date has several', () => {
    const list = nights(
      [
        session({ sleep_id: 'short', duration_minutes: 200, sleep_score: 40 }),
        session({ sleep_id: 'long', duration_minutes: 540, sleep_score: 80 }),
        session({ sleep_id: 'nap', duration_minutes: 900, stages: [], sleep_score: null }),
      ],
      0,
    );
    expect(list).toHaveLength(1);
    expect(list[0].durationMinutes).toBe(540);
    expect(list[0].score).toBe(80);
  });
  it('drops dates that only have sessions without stages', () => {
    expect(nights([session({ stages: [] })], 0)).toEqual([]);
  });
  it('ignores an unknown stage name instead of failing', () => {
    const n = nights([session({ stages: [{ stage: 'weird', minutes: 5 }] })], 0);
    expect(n[0]).toMatchObject({ deep: 0, light: 0, rem: 0, awake: 0 });
  });
});

describe('mean', () => {
  it('averages present values and reports how many there were', () => {
    expect(mean([10, null, 20, undefined])).toEqual({ value: 15, n: 2 });
  });
  it('is null over nothing', () => {
    expect(mean([])).toEqual({ value: null, n: 0 });
    expect(mean([null])).toEqual({ value: null, n: 0 });
  });
});

describe('weightChange', () => {
  it('reports the last weight and the change from the first', () => {
    const rows = [
      { timestamp: '2026-09-29T15:41:38+00:00', weight_kg: 94.1 },
      { timestamp: '2026-09-30T01:47:25+00:00', weight_kg: 92.5 },
    ];
    const r = weightChange(rows);
    expect(r.last).toBe(92.5);
    expect(r.delta).toBeCloseTo(-1.6, 5);
    expect(r.n).toBe(2);
  });
  it('has no change for a single measurement and nothing for none', () => {
    expect(weightChange([{ timestamp: 't', weight_kg: 90 }])).toEqual({
      last: 90,
      delta: null,
      n: 1,
    });
    expect(weightChange([])).toEqual({ last: null, delta: null, n: 0 });
  });
});

describe('fetchAllPages', () => {
  const pager = (total: number, calls: number[]) => async (limit: number, offset: number) => {
    calls.push(offset);
    const data = Array.from({ length: Math.max(0, Math.min(limit, total - offset)) }, (_, i) => ({
      i: offset + i,
    }));
    return { data, count: data.length, total };
  };
  it('pages until every row is fetched', async () => {
    const calls: number[] = [];
    const rows = await fetchAllPages(pager(25, calls), 10);
    expect(rows).toHaveLength(25);
    expect(calls).toEqual([0, 10, 20]);
  });
  it('makes one request when the first page holds everything', async () => {
    const calls: number[] = [];
    expect(await fetchAllPages(pager(3, calls), 10)).toHaveLength(3);
    expect(calls).toEqual([0]);
  });
  it('stops when the server returns an empty page before reaching total', async () => {
    let n = 0;
    const rows = await fetchAllPages(async () => {
      n++;
      return n === 1 ? { data: [1, 2], count: 2, total: 100 } : { data: [], count: 0, total: 100 };
    }, 2);
    expect(rows).toEqual([1, 2]);
    expect(n).toBe(2);
  });
});
