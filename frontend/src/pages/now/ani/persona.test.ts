import { beforeEach, describe, expect, it, vi } from 'vitest';

import { api } from '@/lib/api/client';
import type { Today } from '@/lib/now/types';

import { compact, instructions, tools } from './persona';

vi.mock('@/lib/api/client', () => ({ api: { getJson: vi.fn() } }));
const getJson = vi.mocked(api.getJson);

describe('compact', () => {
  it('drops ids, nulls, empty lists and errors, keeps the rest', () => {
    expect(
      compact({
        id: 'a',
        errors: ['x'],
        tasks: { late_count: 2, first_late: { id: 'b', title: 'Call', due: '2026-10-20' } },
        trip_id: 'c',
        note: null,
        left: [],
        habits: [{ id: 'd', name: 'Run' }],
      }),
    ).toEqual({
      tasks: { late_count: 2, first_late: { title: 'Call', due: '2026-10-20' } },
      habits: [{ name: 'Run' }],
    });
  });
});

describe('instructions', () => {
  it('carry the name, the local time and the digest without ids', () => {
    const today = {
      tasks: { late_count: 1, first_late: { id: 'x', title: 'Pay rent', due: '2026-10-24' } },
    };
    const text = instructions({
      name: 'Ada',
      now: new Date('2026-10-25T06:30:00Z'),
      tz: 'Asia/Almaty',
      today: today as unknown as Today,
    });
    expect(text).toContain('по имени Ada');
    expect(text).toContain('Asia/Almaty');
    expect(text).toContain('Pay rent');
    expect(text).not.toContain('"id"');
    expect(text).toContain('по-русски');
  });
});

describe('tools', () => {
  beforeEach(() => getJson.mockReset());

  it('read through the site API in the caller zone', async () => {
    getJson.mockResolvedValue({ data: [] });
    const [day, find, goals, habits] = tools({ tz: 'Asia/Almaty', date: '2026-10-25' });
    await day.run({});
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/tasks/agenda', {
      query: { date: '2026-10-25', tz: 'Asia/Almaty' },
    });
    await find.run({ query: 'rent', status: 'open' });
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/tasks/items', {
      query: { limit: '20', tz: 'Asia/Almaty', q: 'rent', status: 'open' },
    });
    await goals.run({});
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/goals', {
      query: { status: 'active', date: '2026-10-25', tz: 'Asia/Almaty' },
    });
    await habits.run({});
    expect(getJson).toHaveBeenLastCalledWith('/api/v1/habits');
  });
});
