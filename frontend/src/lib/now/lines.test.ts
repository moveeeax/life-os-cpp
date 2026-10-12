import { describe, expect, it } from 'vitest';

import { MONEY_ABOVE_USUAL, quietLines } from './lines';
import type { Today } from './types';

const quiet: Today = {
  tasks: { late_count: 0, first_late: null, today: [], inbox_count: 0 },
  goals: [],
  focus: null,
  errors: [],
};

describe('quietLines', () => {
  it('says nothing when nothing is off', () => {
    expect(quietLines(quiet, 20)).toEqual([]);
  });

  it('puts the late tasks first with the oldest title', () => {
    const t: Today = {
      ...quiet,
      tasks: {
        late_count: 2,
        first_late: { id: 'a', title: 'Pay the rent', due: '2026-10-01' },
        today: [],
        inbox_count: 0,
      },
      goals: [
        { id: 'g', title: 'Read 12 books', kind: 'count', due: '2026-12-31', pace: 'behind' },
      ],
    };
    const lines = quietLines(t, 9);
    expect(lines.map((l) => l.key)).toEqual(['late', 'goal']);
    expect(lines[0].text).toBe('2 late · Pay the rent');
    expect(lines[0].to).toBe('/tasks');
    expect(lines[1].text).toBe('Read 12 books is behind');
    expect(lines[1].to).toBe('/goals');
  });

  it('skips goals on track', () => {
    const t: Today = {
      ...quiet,
      goals: [{ id: 'g', title: 'Run', kind: 'steps', due: '2026-12-01', pace: 'on_track' }],
    };
    expect(quietLines(t, 9)).toEqual([]);
  });

  it('names the protein left only in the evening and only when it is worth it', () => {
    const food = { eaten: { kcal: 1500, protein_g: 80 }, target: { kcal: 2200, protein_g: 142 } };
    const t: Today = { ...quiet, food };
    expect(quietLines(t, 17)).toEqual([]);
    const evening = quietLines(t, 18);
    expect(evening).toHaveLength(1);
    expect(evening[0].text).toBe('62 g protein left');
    expect(evening[0].to).toBe('/food');
    const nearly: Today = { ...quiet, food: { ...food, eaten: { kcal: 2000, protein_g: 125 } } };
    expect(quietLines(nearly, 20)).toEqual([]);
    const noTarget: Today = {
      ...quiet,
      food: { eaten: { kcal: 100, protein_g: 5 }, target: null },
    };
    expect(quietLines(noTarget, 20)).toEqual([]);
  });

  it('names a month above usual when protein is fine', () => {
    const money = [
      { currency: 'USD', today: 12, projection: 900, median3: 1000 },
      { currency: 'KZT', today: 7500, projection: 412000, median3: 350000 },
    ];
    expect(412000 / 350000).toBeGreaterThanOrEqual(MONEY_ABOVE_USUAL);
    const lines = quietLines({ ...quiet, money }, 10);
    expect(lines).toHaveLength(1);
    expect(lines[0].key).toBe('money');
    expect(lines[0].text).toBe('On pace for 412,000 KZT this month, above usual');
    expect(lines[0].to).toBe('/money/reports');
  });

  it('keeps the protein line over the money line and never shows more than three', () => {
    const t: Today = {
      ...quiet,
      tasks: {
        late_count: 1,
        first_late: { id: 'a', title: 'A', due: '2026-10-01' },
        today: [],
        inbox_count: 0,
      },
      goals: [{ id: 'g', title: 'B', kind: 'count', due: '2026-12-31', pace: 'behind' }],
      food: { eaten: { kcal: 0, protein_g: 0 }, target: { kcal: 2000, protein_g: 100 } },
      money: [{ currency: 'KZT', today: 1, projection: 500, median3: 100 }],
    };
    const lines = quietLines(t, 21);
    expect(lines.map((l) => l.key)).toEqual(['late', 'goal', 'protein']);
  });

  it('marks the numbers so Hide numbers can blur them', () => {
    const t: Today = {
      ...quiet,
      tasks: {
        late_count: 3,
        first_late: { id: 'a', title: 'A', due: '2026-10-01' },
        today: [],
        inbox_count: 0,
      },
    };
    expect(quietLines(t, 9)[0].parts).toEqual([{ text: '3', num: true }, { text: ' late · A' }]);
  });
});
