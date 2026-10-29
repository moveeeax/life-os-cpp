// @vitest-environment jsdom
import { act, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { Today } from '@/lib/now/types';

import { dueThisWeek, groupsOf, REVIEW_EVENT, SEEN_KEY, useWeeklyReview, weekOf } from './review';

describe('weekOf', () => {
  it('is the ISO week of a local day', () => {
    expect(weekOf('2026-10-28')).toBe('2026-W44');
    expect(weekOf('2026-11-01')).toBe('2026-W44');
    expect(weekOf('2026-11-02')).toBe('2026-W45');
    expect(weekOf('2027-01-01')).toBe('2026-W53');
  });
});

describe('dueThisWeek', () => {
  it('is due on Sunday from 10:00, once a week', () => {
    expect(dueThisWeek('2026-11-01', 9, null)).toBe(false);
    expect(dueThisWeek('2026-11-01', 10, null)).toBe(true);
    expect(dueThisWeek('2026-11-01', 18, '2026-W44')).toBe(false);
    expect(dueThisWeek('2026-10-31', 12, null)).toBe(false);
  });
});

describe('groupsOf', () => {
  it('takes late tasks, goals behind and habits under their week', () => {
    const today: Today = {
      tasks: {
        late_count: 1,
        first_late: null,
        late: [{ id: 'l', title: 'Late', due: '2026-10-20' }],
        today: [],
        inbox_count: 0,
      },
      goals: [
        {
          id: 'g1',
          title: 'Run',
          kind: 'count',
          due: '2026-12-01',
          pace: 'behind',
          progress: 0.2,
          days_left: 30,
          unit: '',
          detail: {} as never,
        },
        {
          id: 'g2',
          title: 'Read',
          kind: 'count',
          due: '2026-12-01',
          pace: 'on_track',
          progress: 0.6,
          days_left: 30,
          unit: '',
          detail: {} as never,
        },
      ],
      habits: {
        done: 0,
        total: 2,
        left: [],
        week: [
          { name: 'Вода', done: 3, per_week: 7 },
          { name: 'Бег', done: 3, per_week: 3 },
        ],
      },
      focus: null,
      errors: [],
    };
    const g = groupsOf(today);
    expect(g.late.map((t) => t.id)).toEqual(['l']);
    expect(g.behind.map((x) => x.id)).toEqual(['g1']);
    expect(g.habits.map((h) => h.name)).toEqual(['Вода']);
  });
});

describe('useWeeklyReview', () => {
  beforeEach(() => {
    localStorage.clear();
    vi.useFakeTimers();
  });
  afterEach(() => vi.useRealTimers());

  it('opens by itself once Sunday reaches 10:00 on an open page, and says so', async () => {
    vi.setSystemTime(new Date(2026, 10, 1, 9, 59));
    const heard = vi.fn();
    window.addEventListener(REVIEW_EVENT, heard);
    const { result } = renderHook(() => useWeeklyReview('2026-11-01'));
    expect(result.current.open).toBe(false);
    await act(async () => {
      await vi.advanceTimersByTimeAsync(61_000);
    });
    expect(result.current.open).toBe(true);
    expect(heard).toHaveBeenCalledTimes(1);
    window.removeEventListener(REVIEW_EVENT, heard);
  });

  it('opened by hand on Monday still lets Sunday open it', () => {
    vi.setSystemTime(new Date(2026, 9, 26, 12, 0));
    const { result } = renderHook(() => useWeeklyReview('2026-10-26'));
    act(() => {
      window.dispatchEvent(new Event(REVIEW_EVENT));
    });
    expect(result.current.open).toBe(true);
    expect(localStorage.getItem(SEEN_KEY)).toBeNull();
  });
});

describe('reviewText', () => {
  it('lists what is left and says so when nothing is', async () => {
    const { reviewText } = await import('./review');
    expect(reviewText(undefined, '2026-W44')).toBe(
      'Обзор недели 2026-W44\nХвостов нет: задачи, цели и привычки в порядке.',
    );
    const today = {
      tasks: { late: [{ id: '1', title: 'Паспорт' }] },
      goals: [{ id: 'g', title: 'Подушка', pace: 'behind' }],
      habits: { week: [{ name: 'Плавание', done: 1, per_week: 3 }] },
    } as never;
    expect(reviewText(today, '2026-W44')).toBe(
      'Обзор недели 2026-W44\nПросрочено: Паспорт\nЦели отстают: Подушка\nПривычки недобраны: Плавание 1 из 3',
    );
  });
});
