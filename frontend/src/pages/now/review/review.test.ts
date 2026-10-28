import { describe, expect, it } from 'vitest';

import type { Today } from '@/lib/now/types';

import { dueThisWeek, groupsOf, weekOf } from './review';

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
