import { describe, expect, it } from 'vitest';

import {
  budgetState,
  draftFromJob,
  draftProblem,
  formatMoney,
  groupByDay,
  linesToAccept,
  orderWithAdjustments,
  parseErrorText,
  periodFromSearch,
  periodRange,
  shiftPeriod,
  signedAmount,
  targetSize,
  totalsByCurrency,
  transferNeedsReceived,
} from './index';
import type { ParseJob, Transaction } from './types';

const tx = (over: Partial<Transaction>): Transaction =>
  ({
    id: 't',
    type: 'expense',
    date: '2026-10-05',
    time: null,
    account_id: 'a',
    currency: 'KZT',
    amount: 100,
    category_id: 'c',
    merchant: '',
    merchant_key: '',
    name: 'x',
    receipt_amount: null,
    receipt_currency: null,
    fx_note: '',
    adjusts_id: null,
    trip: '',
    note: '',
    source: 'manual',
    status: 'posted',
    external_id: null,
    final_amount: 100,
    adjustments: [],
    created_at: '',
    updated_at: '',
    ...over,
  }) as Transaction;

describe('formatMoney', () => {
  it('uses the currency and its decimals', () => {
    expect(formatMoney(13275.61, 'KZT', 2)).toMatch(/13,275\.61/);
    expect(formatMoney(25000, 'VND', 0)).toMatch(/25,000/);
    expect(formatMoney(25000, 'VND', 0)).not.toMatch(/\.00/);
  });
});

describe('signedAmount', () => {
  it('signs by type; an adjustment keeps its own sign as a charge', () => {
    expect(signedAmount(tx({ type: 'expense', amount: 10 }))).toBe(-10);
    expect(signedAmount(tx({ type: 'income', amount: 10 }))).toBe(10);
    expect(signedAmount(tx({ type: 'fx_adjustment', amount: 5 }))).toBe(-5);
    expect(signedAmount(tx({ type: 'fx_adjustment', amount: -5 }))).toBe(5);
  });
});

describe('periods', () => {
  it('reads the URL and falls back to this month', () => {
    expect(
      periodFromSearch(new URLSearchParams('kind=week&date=2026-10-08'), '2026-10-07'),
    ).toEqual({
      kind: 'week',
      date: '2026-10-08',
    });
    expect(periodFromSearch(new URLSearchParams('kind=year&date=x'), '2026-10-07')).toEqual({
      kind: 'month',
      date: '2026-10-07',
    });
  });
  it('mirrors the server ranges', () => {
    expect(periodRange('week', '2026-10-01')).toEqual({ from: '2026-09-28', to: '2026-10-04' });
    expect(periodRange('month', '2026-02-10')).toEqual({ from: '2026-02-01', to: '2026-02-28' });
    expect(periodRange('quarter', '2026-11-15')).toEqual({ from: '2026-10-01', to: '2026-12-31' });
  });
  it('shifts by one period', () => {
    expect(shiftPeriod('month', '2026-01-31', -1)).toBe('2025-12-01');
    expect(shiftPeriod('week', '2026-10-07', 1)).toBe('2026-10-12');
    expect(shiftPeriod('quarter', '2026-10-07', -1)).toBe('2026-07-01');
  });
});

describe('totals and days', () => {
  const rows = [
    tx({ id: '1', date: '2026-10-05', currency: 'KZT', amount: 100 }),
    tx({ id: '2', date: '2026-10-05', currency: 'THB', amount: 50 }),
    tx({ id: '3', date: '2026-10-04', currency: 'KZT', type: 'income', amount: 1000 }),
    tx({ id: '4', date: '2026-10-05', currency: 'KZT', status: 'pending', amount: 999 }),
  ];
  it('keeps currencies apart and skips pending rows', () => {
    expect(totalsByCurrency(rows)).toEqual([
      { currency: 'KZT', income: 1000, expense: 100 },
      { currency: 'THB', income: 0, expense: 50 },
    ]);
  });
  it('groups by day, newest first, with per-currency sums', () => {
    const days = groupByDay(rows.filter((r) => r.status === 'posted'));
    expect(days.map((d) => d.date)).toEqual(['2026-10-05', '2026-10-04']);
    expect(days[0].totals).toEqual([
      { currency: 'KZT', income: 0, expense: 100 },
      { currency: 'THB', income: 0, expense: 50 },
    ]);
  });
});

const job = (result: ParseJob['result']): ParseJob =>
  ({
    id: 'j',
    kind: 'text',
    status: 'done',
    text: '',
    hint_date: '2026-10-07',
    result,
    error: null,
    model: null,
    prompt_tokens: null,
    completion_tokens: null,
    accepted_at: null,
    created_at: '',
    finished_at: null,
  }) as ParseJob;

const line = {
  type: 'expense' as const,
  date: '2026-10-05',
  time: null,
  account_id: 'acc',
  amount: 13275.61,
  merchant: 'BIG C',
  name: 'Groceries',
  category_id: 'cat',
  receipt_amount: 955.75,
  receipt_currency: 'THB',
  fx_note: 'THB -> KZT',
  confidence: 0.9,
  note: '',
};

describe('the parse draft', () => {
  it('keeps numbers as text and requires an account', () => {
    const d = draftFromJob(job([line, { ...line, account_id: null }]));
    expect(d[0].amount).toBe('13275.61');
    expect(draftProblem(d)).toBe('Line 2: pick the account.');
    d[1].account_id = 'acc';
    expect(draftProblem(d)).toBeNull();
    d[1].amount = '0';
    expect(draftProblem(d)).toBe('Line 2: the amount must be above 0.');
    d[1].selected = false;
    expect(draftProblem(d)).toBeNull();
  });
  it('becomes the accept body of the selected lines', () => {
    const d = draftFromJob(job([line, { ...line, name: 'Skip' }]));
    d[1].selected = false;
    expect(linesToAccept(d)).toEqual([
      {
        type: 'expense',
        date: '2026-10-05',
        time: null,
        account_id: 'acc',
        amount: 13275.61,
        merchant: 'BIG C',
        name: 'Groceries',
        category_id: 'cat',
        receipt_amount: 955.75,
        receipt_currency: 'THB',
        fx_note: 'THB -> KZT',
        note: '',
      },
    ]);
  });
});

describe('adjustments', () => {
  it('sit right after their original and on its day', () => {
    const orig = tx({ id: 'o', date: '2026-10-03' });
    const adj = tx({
      id: 'a',
      type: 'fx_adjustment',
      adjusts_id: 'o',
      date: '2026-10-07',
      amount: 120,
    });
    const other = tx({ id: 'x', date: '2026-10-05' });
    const days = groupByDay([adj, other, orig]);
    expect(days.map((d) => d.date)).toEqual(['2026-10-05', '2026-10-03']);
    expect(days[1].rows.map((r) => r.id)).toEqual(['o', 'a']);
    expect(orderWithAdjustments([adj, other]).map((r) => r.id)).toEqual(['a', 'x']);
  });
});

describe('the draft drops a category of the other kind', () => {
  it('keeps a matching one', () => {
    const kinds = new Map([
      ['cat', 'expense'],
      ['inc', 'income'],
    ]);
    const d = draftFromJob(job([line, { ...line, category_id: 'inc' }]), kinds);
    expect(d[0].category_id).toBe('cat');
    expect(d[1].category_id).toBeNull();
    d[1].category_id = 'cat';
    d[1].receipt_amount = '0';
    expect(draftProblem(d)).toBe('Line 2: the receipt needs an amount and a currency code.');
  });
});

describe('small rules', () => {
  it('maps parse errors', () => {
    expect(parseErrorText('invalid_answer: lines must hold 1..100 entries')).toBe(
      'The model did not answer with a usable list: lines must hold 1..100 entries',
    );
    expect(parseErrorText('provider_unavailable: timed out')).toMatch(/did not answer/);
    expect(parseErrorText(null)).toBe('The parse failed.');
  });
  it('asks for the received amount only across currencies', () => {
    expect(transferNeedsReceived('KZT', 'KZT')).toBe(false);
    expect(transferNeedsReceived('KZT', 'THB')).toBe(true);
    expect(transferNeedsReceived(undefined, 'THB')).toBe(false);
  });
  it('scales a photo to the long side', () => {
    expect(targetSize(4000, 3000, 1600)).toEqual({ width: 1600, height: 1200 });
    expect(targetSize(1000, 3000, 1600)).toEqual({ width: 533, height: 1600 });
    expect(targetSize(800, 600, 1600)).toEqual({ width: 800, height: 600 });
  });
  it('grades a budget', () => {
    expect(budgetState(10, null)).toBeNull();
    expect(budgetState(70, 100)).toBe('ok');
    expect(budgetState(85, 100)).toBe('near');
    expect(budgetState(100, 100)).toBe('over');
  });
});
