import { describe, expect, it } from 'vitest';

import type { Today } from '@/lib/now/types';

import { evening, tomorrowOf } from './evening';

const day = (statuses: ('open' | 'in_progress' | 'done')[]): Today => ({
  tasks: {
    late_count: 0,
    first_late: null,
    late: [],
    today: statuses.map((status, i) => ({ id: `t${i}`, title: `T${i}`, status })),
    inbox_count: 0,
  },
  goals: [],
  focus: null,
  errors: [],
});

describe('tomorrowOf', () => {
  it('is the next local day, across months', () => {
    expect(tomorrowOf('2026-10-28')).toBe('2026-10-29');
    expect(tomorrowOf('2026-10-31')).toBe('2026-11-01');
    expect(tomorrowOf('2026-12-31')).toBe('2027-01-01');
  });
});

describe('evening', () => {
  it('counts the day and offers to move what is left', () => {
    expect(evening(day(['done', 'open', 'in_progress']), false)).toEqual({
      text: 'Сделано 1 из 3. Перенести 2 на завтра?',
      move: ['t1', 't2'],
    });
  });

  it('praises a done day and moves nothing', () => {
    expect(evening(day(['done', 'done']), false)).toEqual({
      text: 'Все задачи на сегодня сделаны.',
      move: [],
    });
  });

  it('keeps numbers back while they are hidden', () => {
    expect(evening(day(['done', 'open']), true)).toEqual({
      text: 'Остались задачи на сегодня. Перенести на завтра?',
      move: ['t1'],
    });
  });

  it('has nothing to say without tasks today', () => {
    expect(evening(day([]), false)).toBeNull();
  });
});
