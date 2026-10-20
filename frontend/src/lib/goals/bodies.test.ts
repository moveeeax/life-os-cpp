import { describe, expect, it } from 'vitest';

import type { Goal } from './index';
import { goalBody, goalForm, goalPatch } from './bodies';

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
  it('a number goal sends the series it follows, and none for manual check-ins', () => {
    const f = { ...base, kind: 'number' as const, from: '92', to: '85', unit: 'kg', count: '' };
    expect(goalBody({ ...f, source: 'fitness:weight' }).source).toBe('fitness:weight');
    expect(goalBody({ ...f, source: null })).not.toHaveProperty('source');
    expect(
      goalBody({ ...base, kind: 'count', from: '', to: '', unit: '', count: '3', source: 'x:y' }),
    ).not.toHaveProperty('source');
  });
});

describe('goalPatch', () => {
  const goal = {
    id: 'g1',
    title: 'Weight 75',
    area: 'health',
    kind: 'number',
    why: '',
    start_date: '2026-10-01',
    due: '2026-12-31',
    status: 'active',
    completed_at: null,
    unit: 'kg',
    start_value: 82,
    target_value: 75,
    target_count: null,
    result: null,
    source: 'fitness:weight',
    source_why: null,
  } as unknown as Goal;

  it('is empty when nothing changed', () => {
    expect(goalPatch(goal, goalForm(goal))).toEqual({});
  });

  it('carries only the fields that changed', () => {
    const f = { ...goalForm(goal), title: ' Weight 74 ', to: '74', due: '2027-01-31' };
    expect(goalPatch(goal, f)).toEqual({ title: 'Weight 74', target_value: 74, due: '2027-01-31' });
  });

  it('sends source null to go back to manual check-ins, and a new id to switch', () => {
    expect(goalPatch(goal, { ...goalForm(goal), source: null })).toEqual({ source: null });
    expect(goalPatch(goal, { ...goalForm(goal), source: 'tape:waist' })).toEqual({
      source: 'tape:waist',
    });
  });

  it('a count goal sends its new target and never a source', () => {
    const count = {
      ...goal,
      kind: 'count',
      unit: '',
      start_value: null,
      target_value: null,
      target_count: 12,
      source: null,
    } as Goal;
    expect(goalPatch(count, { ...goalForm(count), count: '10' })).toEqual({ target_count: 10 });
  });
});
