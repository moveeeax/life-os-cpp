import { describe, expect, it } from 'vitest';

import { sparkPoints, trendWords, weekRange, widgetKey } from './widgets';

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
