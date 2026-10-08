import { describe, expect, it } from 'vitest';

import { formatValue, paceWord, shelf } from './index';
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
    ).toBe('behind 3');
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
