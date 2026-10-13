import { describe, expect, it } from 'vitest';

import { glassChart } from './chartTheme';

describe('glassChart', () => {
  it('keeps the series and colours and sets the glass look', () => {
    const o = glassChart({
      series: [{ name: 'Steps', data: [1, 2] }],
      colors: ['#fff'],
      chart: { type: 'bar' },
    });
    expect(o.series).toEqual([{ name: 'Steps', data: [1, 2] }]);
    expect(o.colors).toEqual(['#fff']);
    expect(o.chart?.type).toBe('bar');
    expect(o.chart?.background).toBe('transparent');
    expect(o.theme?.mode).toBe('dark');
    expect(o.tooltip?.theme).toBe('dark');
  });
});
