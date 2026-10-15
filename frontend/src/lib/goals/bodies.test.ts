import { describe, expect, it } from 'vitest';

import { goalBody } from './bodies';

describe('goalBody', () => {
  const base = {
    title: ' Read ',
    area: 'growth' as const,
    due: '2026-12-31',
    today: '2026-10-13',
    why: ' fun ',
  };
  it('a binary goal carries no target', () => {
    expect(goalBody({ ...base, kind: 'binary', from: '', to: '', unit: '', count: '' })).toEqual({
      title: 'Read',
      area: 'growth',
      kind: 'binary',
      due: '2026-12-31',
      start_date: '2026-10-13',
      why: 'fun',
    });
  });
  it('a number goal reads comma decimals and trims the unit', () => {
    const b = goalBody({
      ...base,
      kind: 'number',
      from: '92,4',
      to: '85',
      unit: ' kg ',
      count: '',
    });
    expect(b).toMatchObject({ start_value: 92.4, target_value: 85, unit: 'kg' });
  });
  it('a number goal may start below zero', () => {
    const b = goalBody({ ...base, kind: 'number', from: '-1500', to: '0', unit: '$', count: '' });
    expect(b).toMatchObject({ start_value: -1500, target_value: 0 });
  });
  it('a count goal carries its target', () => {
    expect(
      goalBody({ ...base, kind: 'count', from: '', to: '', unit: '', count: '12' }).target_count,
    ).toBe(12);
  });
});
