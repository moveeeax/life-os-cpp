import { describe, expect, it } from 'vitest';

import { firstFreeSlot, topCategory } from './give';

describe('give', () => {
  it('GiveATask: the first free half hour ahead, on a quarter', () => {
    const free = [
      { start: '2026-11-01T04:00:00Z', end: '2026-11-01T04:20:00Z' },
      { start: '2026-11-01T06:00:00Z', end: '2026-11-01T09:00:00Z' },
    ];
    const now = Date.parse('2026-11-01T06:07:00Z');
    expect(firstFreeSlot(free, now, 'UTC')?.at).toBe('06:15');
    expect(firstFreeSlot(free, Date.parse('2026-11-01T08:50:00Z'), 'UTC')).toBeNull();
    expect(firstFreeSlot(free, now, 'Asia/Tashkent')?.at).toBe('11:15');
  });

  it('GiveARow: the category most rows of the merchant carry', () => {
    expect(
      topCategory([
        { category_id: 'food' },
        { category_id: 'food' },
        { category_id: 'cafe' },
        { category_id: null },
      ]),
    ).toEqual({ id: 'food', count: 2 });
    expect(topCategory([{ category_id: null }])).toBeNull();
  });
});
