import { QueryClient, type InfiniteData } from '@tanstack/react-query';
import { describe, expect, it } from 'vitest';

import { qk } from '@/lib/api/queryKeys';

import { GOAL_WRITES } from './useGoals';
import { FOOD_DIARY_WRITES } from './useFood';
import { HABIT_WRITES } from './useHabits';
import { firstPageOnly, MONEY_WRITES, ROW_WRITES, TRANSFER_WRITES } from './useMoney';

// Now's digest (useToday) carries goals, food and money: a write in a section
// must refresh it too, or Now shows the old numbers for up to a minute.
describe('section writes refresh Now', () => {
  const now = JSON.stringify(qk.now.all());
  for (const [name, keys] of [
    ['goals', GOAL_WRITES],
    ['food diary', FOOD_DIARY_WRITES],
    ['money', MONEY_WRITES],
    ['ledger rows', ROW_WRITES],
    ['transfers', TRANSFER_WRITES],
    ['habits', HABIT_WRITES],
  ] as const) {
    it(name, () => {
      expect(keys.map((k) => JSON.stringify(k))).toContain(now);
    });
  }
});

describe('money writes refresh only what they change', () => {
  const covers = (keys: readonly (readonly unknown[])[], key: readonly unknown[]) =>
    keys.some((p) => p.every((v, i) => key[i] === v));

  it('a row write refreshes rows, balances and reports, not transfers or setup', () => {
    expect(covers(ROW_WRITES, qk.money.transactions({ from: '2026-10-01' }))).toBe(true);
    expect(covers(ROW_WRITES, qk.money.accounts(true))).toBe(true);
    expect(covers(ROW_WRITES, qk.money.balances())).toBe(true);
    expect(covers(ROW_WRITES, qk.money.report('month', '2026-10-18', ''))).toBe(true);
    expect(covers(ROW_WRITES, qk.money.summary('USD', 30))).toBe(true);
    expect(covers(ROW_WRITES, qk.money.inbox())).toBe(true);
    expect(covers(ROW_WRITES, qk.money.transfers('2026-10-01', '2026-10-31'))).toBe(false);
    expect(covers(ROW_WRITES, qk.money.currencies())).toBe(false);
    expect(covers(ROW_WRITES, qk.money.categories(true))).toBe(false);
  });

  it('a transfer write refreshes transfers and balances, not the rows', () => {
    expect(covers(TRANSFER_WRITES, qk.money.transfers('2026-10-01', '2026-10-31'))).toBe(true);
    expect(covers(TRANSFER_WRITES, qk.money.accounts())).toBe(true);
    expect(covers(TRANSFER_WRITES, qk.money.transactions({}))).toBe(false);
  });

  it('firstPageOnly cuts loaded pages back to the first', () => {
    const qc = new QueryClient();
    const key = qk.money.transactions({ from: '2026-10-01' });
    qc.setQueryData<InfiniteData<string, number>>(key, {
      pages: ['p0', 'p1', 'p2'],
      pageParams: [0, 50, 100],
    });
    qc.setQueryData(qk.money.inbox(), ['row']);
    firstPageOnly(qc, ['money', 'transactions']);
    expect(qc.getQueryData(key)).toEqual({ pages: ['p0'], pageParams: [0] });
    expect(qc.getQueryData(qk.money.inbox())).toEqual(['row']);
  });
});
