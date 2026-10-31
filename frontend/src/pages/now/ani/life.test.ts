import { describe, expect, it } from 'vitest';

import {
  achievementsOf,
  anniversaryOf,
  dealLine,
  energyLine,
  energyOf,
  meetingAsk,
  questDone,
  questOf,
} from './life';

describe("Ani's life", () => {
  it('EnergyFromSleep: under six hours is low, seven and a half is high', () => {
    expect(energyOf(330)).toBe('low');
    expect(energyOf(400)).toBe('ok');
    expect(energyOf(460)).toBe('high');
    expect(energyOf(null)).toBeNull();
    expect(energyLine('low')).toMatch(/после обеда/);
    expect(energyLine('ok')).toBeNull();
  });

  it('DailyQuest: an overdue task first, water in the morning, focus otherwise', () => {
    const base = { overdue: 0, waterMl: 0, focusMinutes: 0, hour: 9 };
    expect(questOf({ ...base, overdue: 2 }).id).toBe('overdue');
    expect(questOf(base).id).toBe('water');
    expect(questOf({ ...base, hour: 14 }).id).toBe('focus');
    expect(questOf({ ...base, waterMl: null }).id).toBe('focus');
    const start = { ...base, overdue: 2 };
    expect(questDone(questOf(start), { ...start, overdue: 1 }, start)).toBe(true);
    expect(questDone(questOf(base), { ...base, waterMl: 500 }, base)).toBe(true);
    expect(questDone(questOf({ ...base, hour: 14 }), { ...base, focusMinutes: 19 }, base)).toBe(
      false,
    );
  });

  it('AnniversaryOfACrossing: the same day of an earlier year', () => {
    const crossings = [
      { date: '2025-10-31', to_country: 'UZ' },
      { date: '2026-03-01', to_country: 'TH' },
    ];
    expect(anniversaryOf('2026-10-31', crossings)).toMatch(/Год назад .*Узбекистан/);
    expect(anniversaryOf('2026-10-30', crossings)).toBeNull();
    expect(anniversaryOf('2028-10-31', crossings)).toMatch(/3 года назад/);
  });

  it('AfterAMeeting: asks once within a quarter of an hour of its end', () => {
    const now = Date.parse('2026-10-31T10:10:00Z');
    const events = [
      {
        id: 'a',
        title: 'Sync',
        start: '2026-10-31T09:00:00Z',
        end: '2026-10-31T10:00:00Z',
        all_day: false,
      },
      {
        id: 'b',
        title: 'Short',
        start: '2026-10-31T09:55:00Z',
        end: '2026-10-31T10:05:00Z',
        all_day: false,
      },
    ];
    expect(meetingAsk(events, now, new Set())?.id).toBe('a');
    expect(meetingAsk(events, now, new Set(['a']))).toBeNull();
    expect(meetingAsk(events, Date.parse('2026-10-31T10:30:00Z'), new Set())).toBeNull();
  });

  it('PromiseLines: a nudge on Wednesday, the result on Sunday evening', () => {
    const deals = [{ id: 'd', text: 'Зал три раза', target: 3, done: 1 }];
    expect(dealLine(deals, 3, 12)).toMatch(/Зал три раза.*1 из 3/);
    expect(dealLine([{ ...deals[0], done: 2 }], 3, 12)).toBeNull();
    expect(dealLine([{ ...deals[0], done: 3 }], 0, 19)).toMatch(/выполнен/);
    expect(dealLine(deals, 0, 19)).toMatch(/1 из 3/);
    expect(dealLine(deals, 1, 19)).toBeNull();
  });

  it('Achievements: ten hours of focus in a week, a long session, a kept promise', () => {
    const got = achievementsOf({ week_minutes: 610, longest_minutes: 95 }, [
      { id: 'd', text: 'Зал', target: 3, done: 3 },
      { id: 'e', text: 'Фокус', target: 300, done: 100 },
    ]);
    expect(got.map((a) => a.id)).toEqual(['focus10h', 'deep90', 'deal:d']);
    expect(achievementsOf(null, [])).toEqual([]);
  });
});
