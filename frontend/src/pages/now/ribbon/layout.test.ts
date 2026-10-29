import { describe, expect, it } from 'vitest';

import { blocksOf, nextEvent, nextWords, pctOf, pinsOf, type NowDay } from './layout';

const TZ = 'Asia/Tashkent';
const day: NowDay = {
  date: '2026-10-29',
  calendar: 'ok',
  events: [
    {
      id: 'e1',
      title: 'Standup',
      start: '2026-10-29T04:00:00Z',
      end: '2026-10-29T04:30:00Z',
      all_day: false,
      location: '',
    },
    {
      id: 'e2',
      title: 'Call with Anna',
      start: '2026-10-29T06:00:00Z',
      end: '2026-10-29T07:00:00Z',
      all_day: false,
      location: '',
    },
    {
      id: 'b',
      title: 'Birthday',
      start: '2026-10-29',
      end: '2026-10-30',
      all_day: true,
      location: '',
    },
    {
      id: 'late',
      title: 'Late',
      start: '2026-10-29T17:30:00Z',
      end: '2026-10-29T19:30:00Z',
      all_day: false,
      location: '',
    },
  ],
  free: [{ start: '2026-10-29T04:30:00Z', end: '2026-10-29T06:00:00Z', minutes: 90 }],
  focus: [{ start: '2026-10-29T03:30:00Z', end: '2026-10-29T04:00:00Z', minutes: 30, label: '' }],
  workouts: [{ start: '2026-10-29T13:00:00Z', end: null, name: 'Gym' }],
  tasks: [{ id: 't1', title: 'Pay rent', time: '13:00', status: 'open' }],
};

describe('ribbon', () => {
  it('RibbonLayout: hours 8..23 to 0..100', () => {
    expect(pctOf(8 * 60)).toBe(0);
    expect(pctOf(23 * 60)).toBe(100);
    expect(pctOf(15 * 60 + 30)).toBe(50);
    expect(pctOf(2 * 60)).toBe(0);
  });

  it('RibbonOpenEnds: a running workout ends now, an event past midnight is clipped, all-day left out', () => {
    const now = Date.parse('2026-10-29T13:30:00Z'); // 18:30 local
    const blocks = blocksOf(day, TZ, now);
    const gym = blocks.find((b) => b.kind === 'gym')!;
    expect(gym.end).toBe(now);
    expect(Math.round(gym.left)).toBe(Math.round(pctOf(18 * 60)));
    expect(blocks.some((b) => b.title === 'Birthday')).toBe(false);
    const late = blocks.find((b) => b.title === 'Late')!;
    expect(late.left + late.width).toBe(100);
    expect(blocks.find((b) => b.kind === 'did')?.left).toBeCloseTo(pctOf(8 * 60 + 30));
    expect(pinsOf(day)[0].left).toBeCloseTo(pctOf(13 * 60));
  });

  it('RibbonOpenEnds: a running focus is drawn from its start to now', () => {
    const now = Date.parse('2026-10-29T10:00:00Z'); // 15:00 local
    const running = blocksOf(day, TZ, now, Date.parse('2026-10-29T09:20:00Z'));
    const live = running.find((b) => b.key === 'focus:running')!;
    expect(live.kind).toBe('did');
    expect(live.end).toBe(now);
    expect(live.left).toBeCloseTo(pctOf(14 * 60 + 20));
    expect(blocksOf(day, TZ, now).some((b) => b.key === 'focus:running')).toBe(false);
  });

  it('NextEventLine: the next one, the running one, then a time past an hour', () => {
    expect(nextEvent(day, Date.parse('2026-10-29T05:35:00Z'))).toEqual({
      title: 'Call with Anna',
      minutes: 25,
      id: 'e2',
    });
    expect(nextWords({ title: 'Call with Anna', minutes: 25 }, '11:00')).toBe(
      'in 25 min · Call with Anna',
    );
    expect(nextWords(nextEvent(day, Date.parse('2026-10-29T06:10:00Z')), '11:00')).toBe(
      'now · Call with Anna',
    );
    expect(nextWords({ title: 'X', minutes: 90 }, '14:00')).toBe('at 14:00 · X');
    expect(nextEvent(day, Date.parse('2026-10-29T20:00:00Z'))).toBeNull();
  });
});
