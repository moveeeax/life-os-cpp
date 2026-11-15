import { describe, expect, it } from 'vitest';

import { manualBody, transferBody, transferProblem } from './bodies';

describe('manualBody', () => {
  it('posts what the manual form posted', () => {
    expect(
      manualBody({
        type: 'expense',
        date: '2026-10-13',
        account: 'a1',
        amount: 5000,
        merchant: '  Magnum ',
        name: ' ',
        category: 'c1',
        receiptAmount: '',
        receiptCurrency: '',
        ra: null,
        note: 'weekly',
      }),
    ).toEqual({
      type: 'expense',
      date: '2026-10-13',
      account_id: 'a1',
      amount: 5000,
      merchant: 'Magnum',
      name: 'Magnum',
      category_id: 'c1',
      receipt_amount: null,
      receipt_currency: null,
      note: 'weekly',
      source: 'manual',
      status: 'posted',
    });
  });
  it('keeps a receipt in another currency', () => {
    const b = manualBody({
      type: 'expense',
      date: '2026-10-13',
      account: 'a1',
      amount: 10,
      merchant: '',
      name: 'Lunch',
      category: 'c1',
      receiptAmount: '350',
      receiptCurrency: 'THB',
      ra: 350,
      note: '',
    });
    expect(b.receipt_amount).toBe(350);
    expect(b.receipt_currency).toBe('THB');
    expect(b.name).toBe('Lunch');
  });
});

describe('transferBody', () => {
  it('posts what the transfer form posted', () => {
    expect(
      transferBody({
        date: '2026-10-12',
        from: 'w',
        to: 'k',
        sent: 10,
        received: 4900,
        needsReceived: true,
        fee: '1',
        f: 1,
        fromName: 'Wise',
        toName: 'Kaspi',
        note: '',
      }),
    ).toEqual({
      date: '2026-10-12',
      from_account_id: 'w',
      to_account_id: 'k',
      amount_sent: 10,
      amount_received: 4900,
      fee: 1,
      name: 'Wise → Kaspi',
      note: '',
    });
  });
  it('sends no received amount within one currency and no fee when empty', () => {
    const b = transferBody({
      date: '2026-10-12',
      from: 'w',
      to: 'k',
      sent: 10,
      received: null,
      needsReceived: false,
      fee: ' ',
      f: null,
      fromName: 'A',
      toName: 'B',
      note: 'x',
    });
    expect(b.amount_received).toBeNull();
    expect(b.fee).toBeNull();
  });
});

describe('transferProblem', () => {
  const ok = {
    date: '2026-10-12',
    sent: 10,
    received: null,
    needsReceived: false,
    fee: '',
    f: null,
  };
  it('lets a plain transfer go', () => {
    expect(transferProblem(ok)).toBeNull();
    expect(transferProblem({ ...ok, fee: '0', f: 0 })).toBeNull();
    expect(transferProblem({ ...ok, needsReceived: true, received: 4 })).toBeNull();
  });
  it('names the first field that is wrong', () => {
    expect(transferProblem({ ...ok, sent: null })).toBe('sent');
    expect(transferProblem({ ...ok, sent: 0 })).toBe('sent');
    expect(transferProblem({ ...ok, needsReceived: true })).toBe('received');
    expect(transferProblem({ ...ok, needsReceived: true, received: 0 })).toBe('received');
    expect(transferProblem({ ...ok, fee: 'x', f: null })).toBe('fee');
    expect(transferProblem({ ...ok, fee: '-1', f: -1 })).toBe('fee');
    expect(transferProblem({ ...ok, date: '2026-13-40' })).toBe('date');
  });
});
