import { describe, expect, it } from 'vitest';

import { formatAmount } from './amounts';

// One formatter behind five old ones; each block pins what one caller printed.

describe('formatAmount: the kit (true minus, code after)', () => {
  it('groups, signs and appends the code', () => {
    expect(formatAmount(-5000, { currency: 'KZT', digits: 0 })).toBe('−5,000 KZT');
    expect(formatAmount(460000, { currency: 'KZT', signed: true, digits: 0 })).toBe('+460,000 KZT');
    expect(formatAmount(12.5)).toBe('12.50');
    expect(formatAmount(0, { signed: true })).toBe('0.00');
  });
});

describe('formatAmount: billing cents (no grouping)', () => {
  it('prints plain two-decimal dollars', () => {
    expect(formatAmount(500 / 100, { grouping: false })).toBe('5.00');
    expect(formatAmount(1234 / 100, { grouping: false })).toBe('12.34');
    expect(formatAmount(123456789 / 100, { grouping: false })).toBe('1234567.89');
    expect(formatAmount(0, { grouping: false })).toBe('0.00');
  });
});

describe('formatAmount: Money summary totals (whole from a thousand)', () => {
  it('keeps cents under a thousand and drops them above', () => {
    const total = { wholeFrom: 1000, trueMinus: false } as const;
    expect(formatAmount(912.5, total)).toBe('912.50');
    expect(formatAmount(12345.678, total)).toBe('12,346');
    expect(formatAmount(-0.4, total)).toBe('-0.40');
    expect(formatAmount(-1500.4, total)).toBe('-1,500');
  });

  it('a change: always a sign, a true minus', () => {
    const change = { wholeFrom: 1000, signed: true, zeroSign: '+' } as const;
    expect(formatAmount(1240, change)).toBe('+1,240');
    expect(formatAmount(-310.5, change)).toBe('−310.50');
    expect(formatAmount(0, change)).toBe('+0.00');
  });
});

describe("formatAmount: Now's day panel (whole stays whole)", () => {
  it('shows cents only when there are some', () => {
    const now = { wholeWhenWhole: true, trueMinus: false } as const;
    expect(formatAmount(4.5, now)).toBe('4.50');
    expect(formatAmount(2790, now)).toBe('2,790');
    expect(formatAmount(13275.61, now)).toBe('13,275.61');
    expect(formatAmount(2790.004, now)).toBe('2,790');
  });
});

describe('formatAmount: the ledger (currency symbol)', () => {
  it("uses the currency's own sign and the given decimals", () => {
    expect(formatAmount(13275.61, { currency: 'KZT', symbol: true })).toMatch(/13,275\.61/);
    expect(formatAmount(25000, { currency: 'VND', symbol: true, digits: 0 })).toMatch(/25,000/);
    expect(formatAmount(25000, { currency: 'VND', symbol: true, digits: 0 })).not.toMatch(/\.00/);
    expect(formatAmount(12.5, { currency: 'USD', symbol: true })).toBe('$12.50');
    expect(formatAmount(-12.5, { currency: 'USD', symbol: true })).toBe('-$12.50');
  });

  it('an unknown code falls back to the plain number and the code', () => {
    expect(formatAmount(1234.5, { currency: 'NOT A CODE', symbol: true })).toBe(
      '1234.50 NOT A CODE',
    );
  });
});
