import { describe, expect, it } from 'vitest';

import { mealAt, quickFoods, sparkPoints, trendWords, weekRange, widgetKey } from './widgets';

describe('widgets', () => {
  it('words a trend against the days before', () => {
    expect(trendWords([56, 57, 56, 58, 61], 'bpm')).toBe('4.3 bpm above usual');
    expect(trendWords([7000, 8000, 7500, 6000], '')).toBe('1500 below usual');
    expect(trendWords([80, 80, 80, 81], '')).toBe('as usual');
    expect(trendWords([1, 2], '')).toBe('');
    expect(trendWords([null, 5, null, 6, 7, 8], '')).toBe('2 above usual');
  });
  it('draws points and finds the week', () => {
    expect(sparkPoints([1, null, 3])).toBe('0.0,30.0 100.0,4.0');
    expect(sparkPoints([1])).toBe('');
    expect(weekRange('2026-10-29')).toMatchObject({ from: '2026-10-26', to: '2026-11-01' });
    expect(widgetKey({ kind: 'series', source: 'fitness:steps' })).toBe('series:fitness:steps');
  });
});

describe('water and protein', () => {
  const item = (
    id: string,
    protein_g: number,
    servings: { label: string; grams: number }[] = [],
  ) => ({ id, name: id, protein_g, servings });

  it('QuickProteinFoods: the two richest recent products, a serving or 100 g each', () => {
    const quick = quickFoods([
      item('rice', 2),
      item('skyr', 11, [{ label: 'cup', grams: 150 }]),
      item('chicken', 23),
    ]);
    expect(quick.map((q) => q.item.id)).toEqual(['chicken', 'skyr']);
    expect(quick.map((q) => q.grams)).toEqual([100, 150]);
    expect(quickFoods([item('rice', 2)])).toEqual([]);
  });

  it('a meal by the hour of the day', () => {
    expect([7, 12, 19, 23].map(mealAt)).toEqual(['breakfast', 'lunch', 'dinner', 'snack']);
  });
});
