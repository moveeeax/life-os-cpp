import { describe, expect, it } from 'vitest';

import type { Transaction } from './types';
import { convertSum, groupByWeek, median, periodWeeks, tone, toneColor } from './weeks';

const row = (
  id: string,
  date: string,
  amount: number,
  currency: string,
  type = 'expense',
  adjusts_id: string | null = null,
) => ({ id, date, amount, currency, type, status: 'posted', adjusts_id }) as unknown as Transaction;

describe('ledger weeks', () => {
  it('cuts October 2026 into five Monday to Sunday weeks, latest first', () => {
    expect(periodWeeks('2026-10-01', '2026-10-31')).toEqual([
      { from: '2026-10-26', to: '2026-10-31' },
      { from: '2026-10-19', to: '2026-10-25' },
      { from: '2026-10-12', to: '2026-10-18' },
      { from: '2026-10-05', to: '2026-10-11' },
      { from: '2026-10-01', to: '2026-10-04' },
    ]);
  });

  it('groups rows by week, latest day first, a recalculation with its original', () => {
    const weeks = groupByWeek(
      [
        row('a', '2026-10-26', 100000, 'UZS'),
        row('b', '2026-10-27', 5.72, 'USD'),
        row('c', '2026-10-25', 7500, 'UZS'),
        row('d', '2026-10-27', 1, 'USD', 'fx_adjustment', 'c'),
      ],
      '2026-10-01',
      '2026-10-31',
    );
    expect(weeks[0].rows.map((r) => r.id)).toEqual(['b', 'a']);
    expect(weeks[1].rows.map((r) => r.id)).toEqual(['c', 'd']);
    expect(weeks[4].rows).toEqual([]);
    expect(weeks[0].totals).toEqual(
      [
        { currency: 'USD', income: 0, expense: 5.72 },
        { currency: 'UZS', income: 0, expense: 100000 },
      ].sort((x, y) => x.currency.localeCompare(y.currency)),
    );
  });

  it('converts sums with the report rates and marks a missing rate', () => {
    const rates = { USD: { per_usd: 1 }, KZT: { per_usd: 500 }, UZS: { per_usd: 12500 } };
    expect(
      convertSum(
        [
          { currency: 'USD', amount: 2 },
          { currency: 'UZS', amount: 25000 },
          { currency: 'KZT', amount: 10 },
        ],
        rates,
        'KZT',
      ),
    ).toEqual({ amount: 2010, partial: false });
    expect(
      convertSum(
        [
          { currency: 'THB', amount: 740 },
          { currency: 'KZT', amount: 10 },
        ],
        rates,
        'KZT',
      ),
    ).toEqual({ amount: 10, partial: true });
  });

  it('puts months on the scale around the median', () => {
    expect(median([0, 300, 100, 200])).toBe(200);
    expect(median([])).toBeNull();
    expect(tone(200, 200)).toBe(0.5);
    expect(tone(100, 200)).toBe(0);
    expect(tone(800, 200)).toBe(1);
    expect(toneColor(0).dark).toBe(true);
    expect(toneColor(1)).toEqual({ background: 'rgb(62, 52, 200)', dark: false });
  });
});
