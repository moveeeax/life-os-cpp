import { describe, expect, it } from 'vitest';

import { SMALL_LIMITS, valueAccounts, viewAccounts } from './accountsView';

const acc = (id: string, currency: string, balance: number, name = id) => ({
  id,
  name,
  currency,
  balance,
});

// Units of the account currency per 1 USD, as /money/summary gives them.
const rows = [
  { id: 'kzt', currency: 'KZT', rate: 500, value: 1000 },
  { id: 'btc', currency: 'BTC', rate: 0.00001, value: 5000 },
];

describe('valueAccounts', () => {
  it('takes the value of an account the summary converted', () => {
    const [v] = valueAccounts([acc('btc', 'BTC', 0.05)], rows);
    expect(v.value).toBe(5000);
  });

  it('converts an account the summary left out with the rate of its currency', () => {
    // An archived KZT account: not in the summary, but KZT has a rate there.
    const [v] = valueAccounts([acc('old', 'KZT', 2500)], rows);
    expect(v.value).toBe(5);
  });

  it('has no value for a currency without a rate', () => {
    const [v] = valueAccounts([acc('thb', 'THB', 100)], rows);
    expect(v.value).toBeNull();
  });
});

describe('viewAccounts', () => {
  const list = [
    { account: acc('a', 'KZT', 1, 'Alpha'), value: 0.002 },
    { account: acc('b', 'BTC', 0.05, 'Bitcoin'), value: 5000 },
    { account: acc('c', 'USD', 800, 'Wise'), value: 800 },
    { account: acc('d', 'THB', 100, 'Cash'), value: null },
  ];

  it('sorts by the value, largest first, an unknown value last', () => {
    expect(viewAccounts(list, 0).shown.map((v) => v.account.id)).toEqual(['b', 'c', 'a', 'd']);
  });

  it('hides an account under the limit, by its value and not its balance', () => {
    const v = viewAccounts(list, 10);
    // BTC 0.05 is a small number worth 5000 and stays; KZT 1 is worth 0.002 and goes.
    expect(v.shown.map((x) => x.account.id)).toEqual(['b', 'c', 'd']);
    expect(v.hidden).toBe(1);
  });

  it('never hides an account it cannot value', () => {
    expect(viewAccounts(list, 1000).shown.map((x) => x.account.id)).toEqual(['b', 'd']);
  });

  it('measures a negative balance by its size', () => {
    const debt = [{ account: acc('x', 'USD', -300, 'Card'), value: -300 }];
    expect(viewAccounts(debt, 100).shown).toHaveLength(1);
  });

  it('breaks ties by name', () => {
    const tie = [
      { account: acc('2', 'USD', 5, 'Zed'), value: 5 },
      { account: acc('1', 'USD', 5, 'Ann'), value: 5 },
    ];
    expect(viewAccounts(tie, 0).shown.map((x) => x.account.name)).toEqual(['Ann', 'Zed']);
  });

  it('offers no limit first', () => {
    expect(SMALL_LIMITS[0]).toBe(0);
  });
});
