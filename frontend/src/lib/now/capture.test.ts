import { describe, expect, it } from 'vitest';

import { spendFacts } from './capture';

const kaspi = { id: 'a1', name: 'Kaspi', currency: 'KZT' };

describe('spendFacts', () => {
  it('names the amount with the currency and the account', () => {
    expect(spendFacts({ amount: '2500', account_id: 'a1' }, [kaspi])).toEqual({
      amount: '2500 KZT',
      account: 'Kaspi',
    });
  });
  it('says when the account is missing or unknown', () => {
    expect(spendFacts({ amount: '120', account_id: null }, [kaspi])).toEqual({
      amount: '120',
      account: 'no account',
    });
    expect(spendFacts({ amount: '120', account_id: 'gone' }, [kaspi]).account).toBe('no account');
  });
});
