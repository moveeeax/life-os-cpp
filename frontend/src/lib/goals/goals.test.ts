import { describe, expect, it } from 'vitest';

import {
  axisLabels,
  formatValue,
  goalChart,
  lastSourceDate,
  paceWord,
  shelf,
  weeklyPace,
} from './index';
import type { Goal } from './types';

const base = {
  id: 'g',
  title: 'Goal',
  area: 'health',
  why: '',
  start_date: '2026-09-01',
  due: '2027-03-31',
  status: 'active',
  completed_at: null,
  unit: '',
  start_value: null,
  target_value: null,
  target_count: null,
  result: null,
  created_at: '',
  updated_at: '',
} as const;

const goal = (over: Partial<Goal>): Goal => ({ ...base, kind: 'number', ...over }) as Goal;

describe('paceWord', () => {
  it('names a number goal behind, on track or not updated', () => {
    const p = {
      kind: 'number',
      elapsed: 0.2,
      days_left: 100,
      progress: 0.1,
      pace: 'behind',
      gap: -1.76,
      stale: false,
    };
    expect(paceWord(goal({ unit: 'kg', progress: p as Goal['progress'] }))).toEqual({
      text: 'behind 1.8 kg',
      tone: 'behind',
    });
    expect(
      paceWord(
        goal({ unit: 'kg', progress: { ...p, pace: 'on_track', gap: 0.4 } as Goal['progress'] }),
      ).text,
    ).toBe('on track');
    expect(
      paceWord(goal({ unit: 'kg', progress: { ...p, stale: true } as Goal['progress'] })),
    ).toEqual({ text: 'not updated', tone: 'stale' });
  });
  it('names steps, count and binary goals', () => {
    expect(
      paceWord(
        goal({
          kind: 'steps',
          progress: {
            kind: 'steps',
            elapsed: 0.5,
            days_left: 10,
            progress: 0.2,
            pace: 'behind',
          } as Goal['progress'],
        }),
      ).text,
    ).toBe('behind');
    expect(
      paceWord(
        goal({
          kind: 'count',
          progress: {
            kind: 'count',
            elapsed: 0.77,
            days_left: 84,
            progress: 0.58,
            pace: 'behind',
            done: 7,
            expected: 9.2,
            reached: false,
          } as Goal['progress'],
        }),
      ).text,
    ).toBe('behind 2');
    // The server's pace floors expected: 3.2 expected with 2 done is one behind.
    expect(
      paceWord(
        goal({
          kind: 'count',
          progress: {
            kind: 'count',
            elapsed: 0.32,
            days_left: 60,
            progress: 0.2,
            pace: 'behind',
            done: 2,
            expected: 3.2,
            reached: false,
          } as Goal['progress'],
        }),
      ).text,
    ).toBe('behind 1');
    expect(
      paceWord(
        goal({
          kind: 'count',
          progress: {
            kind: 'count',
            elapsed: 0.9,
            days_left: 9,
            progress: 1,
            pace: 'on_track',
            done: 12,
            expected: 10.8,
            reached: true,
          } as Goal['progress'],
        }),
      ),
    ).toEqual({ text: 'reached', tone: 'ok' });
    expect(
      paceWord(
        goal({
          kind: 'binary',
          progress: {
            kind: 'binary',
            elapsed: 0.7,
            days_left: 38,
            progress: null,
            pace: 'on_track',
          } as Goal['progress'],
        }),
      ).text,
    ).toBe('38 days left');
    expect(
      paceWord(
        goal({
          kind: 'binary',
          progress: {
            kind: 'binary',
            elapsed: 0.7,
            days_left: 1,
            progress: null,
            pace: 'behind',
          } as Goal['progress'],
        }),
      ).text,
    ).toBe('1 day left');
    expect(
      paceWord(
        goal({
          kind: 'binary',
          progress: {
            kind: 'binary',
            elapsed: 1,
            days_left: 0,
            progress: null,
            pace: 'failed',
          } as Goal['progress'],
        }),
      ),
    ).toEqual({ text: 'not passed', tone: 'fail' });
  });
});

describe('formatValue', () => {
  it('groups thousands for large values and keeps one decimal otherwise', () => {
    expect(formatValue(2050000, 'KZT')).toBe('2,050,000 KZT');
    expect(formatValue(91.64, 'kg')).toBe('91.6 kg');
    expect(formatValue(7, '')).toBe('7');
  });
});

describe('shelf', () => {
  it('fills the done cells and dashes the next one', () => {
    expect(shelf(4, 2)).toEqual(['done', 'done', 'next', 'empty']);
    expect(shelf(3, 3)).toEqual(['done', 'done', 'done']);
    expect(shelf(2, 5)).toEqual(['done', 'done']);
  });
});

describe('weeklyPace', () => {
  it('says reached at and past the target, and signs only a non-zero pace', () => {
    expect(weeklyPace(1, 0, 'kg')).toEqual({ value: 'reached', label: 'the target' });
    expect(weeklyPace(1, -0.4, 'kg').value).toBe('reached');
    expect(weeklyPace(0.5, -0.4, 'kg')).toEqual({ value: '−0.4 kg', label: 'a week to make it' });
    expect(weeklyPace(0.5, 0.3, 'kg').value).toBe('+0.3 kg');
    expect(weeklyPace(0.99, 0.01, 'kg').value).toBe('0 kg');
  });
});

describe('goalChart', () => {
  const checkins = [
    { id: 'c2', date: '2026-03-01', value: 80, note: '' },
    { id: 'c1', date: '2026-01-01', value: 84, note: '' },
  ];
  const base = { start_date: '2026-01-01', start_value: 85 };

  it('starts the line at a check-in dated on the start date', () => {
    expect(goalChart({ ...base, checkins, points: [] })).toEqual([
      { date: '2026-01-01', value: 84, from: 'checkin' },
      { date: '2026-03-01', value: 80, from: 'checkin' },
    ]);
    expect(goalChart({ ...base, checkins: checkins.slice(0, 1), points: [] })).toEqual([
      { date: '2026-01-01', value: 85, from: 'start' },
      { date: '2026-03-01', value: 80, from: 'checkin' },
    ]);
  });

  it('draws the points of a source with their origin', () => {
    const points = [
      { date: '2026-01-02', value: 84.6, from: 'source' as const },
      { date: '2026-01-03', value: 84, from: 'checkin' as const },
      { date: '2026-01-04', value: 84.2, from: 'source' as const },
    ];
    expect(goalChart({ ...base, checkins: [], points })).toEqual([
      { date: '2026-01-01', value: 85, from: 'start' },
      ...points,
    ]);
    expect(lastSourceDate(points)).toBe('2026-01-04');
    expect(lastSourceDate(points.filter((p) => p.from === 'checkin'))).toBeNull();
  });
});

describe('axisLabels', () => {
  const start = { x: 46, text: '19 Oct' };
  const end = { x: 628, text: '31 Dec' };
  const texts = (today: number | null) => axisLabels({ start, end, today }).map((l) => l.text);

  it('drops the start date when today sits on it', () => {
    // The owner's screenshot: a goal started today, "today" over "19 Oct".
    expect(texts(46)).toEqual(['31 Dec', 'today']);
    expect(texts(100)).toEqual(['31 Dec', 'today']);
  });

  it('keeps both dates when today is clear of them', () => {
    expect(texts(300)).toEqual(['19 Oct', '31 Dec', 'today']);
    // 46 + 44 = 90 is the right edge of the start label; today's left edge is 112 - 22 = 90.
    expect(texts(112)).toEqual(['19 Oct', '31 Dec', 'today']);
  });

  it('drops the due date when today is near it, and draws no today outside the goal', () => {
    expect(texts(610)).toEqual(['19 Oct', 'today']);
    expect(texts(null)).toEqual(['19 Oct', '31 Dec']);
  });

  it('anchors each label at its side', () => {
    expect(axisLabels({ start, end, today: 300 }).map((l) => l.anchor)).toEqual([
      'start',
      'end',
      'middle',
    ]);
  });
});
