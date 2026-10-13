import { describe, expect, it } from 'vitest';

import {
  SUMMARY_CURRENCIES,
  cardCurrency,
  chartGeometry,
  formatTotal,
  rateDay,
  rateText,
  signed,
} from './networth';

describe('formatTotal', () => {
  it('keeps cents under a thousand and drops them above', () => {
    expect(formatTotal(912.5)).toBe('912.50');
    expect(formatTotal(12345.678)).toBe('12,346');
    expect(formatTotal(-0.4)).toBe('-0.40');
  });
});

describe('signed', () => {
  it('writes a sign in front', () => {
    expect(signed(1240)).toBe('+1,240');
    expect(signed(-310.5)).toBe('−310.50');
    expect(signed(0)).toBe('+0.00');
  });
});

describe('rateText', () => {
  it('reads as one target unit in the account currency', () => {
    expect(rateText(490, 'KZT', 'USD')).toBe('1 USD = 490.00 KZT');
    expect(rateText(1, 'USD', 'USD')).toBe('—');
  });
  it('turns around when the number would be under one', () => {
    // 1 RUB = 0.01225 USD reads better as 1 USD = 81.60 RUB.
    expect(rateText(1 / 81.6, 'USD', 'RUB')).toBe('1 USD = 81.60 RUB');
    expect(rateText(0.9183, 'EUR', 'USD')).toBe('1 EUR = 1.09 USD');
  });
});

describe('the currency switcher', () => {
  it('offers USD, EUR, KZT and RUB', () => {
    expect(SUMMARY_CURRENCIES).toEqual(['USD', 'EUR', 'KZT', 'RUB']);
  });
});

describe('chartGeometry', () => {
  const days = [
    { date: '2026-10-11', total: 100, partial: false },
    { date: '2026-10-12', total: 150, partial: false },
    { date: '2026-10-13', total: 125, partial: false },
  ];
  it('maps the first and last day to the plot edges and keeps the values inside', () => {
    const g = chartGeometry(days, {
      width: 700,
      height: 200,
      left: 50,
      right: 10,
      top: 10,
      bottom: 20,
    });
    expect(g.points[0].x).toBe(50);
    expect(g.points[2].x).toBe(690);
    for (const p of g.points) {
      expect(p.y).toBeGreaterThanOrEqual(10);
      expect(p.y).toBeLessThanOrEqual(180);
    }
    expect(g.points[1].y).toBeLessThan(g.points[0].y);
    expect(g.line.startsWith('M50,')).toBe(true);
    expect(g.ticks.length).toBe(5);
  });
  it('draws a flat line through the middle when every day is the same', () => {
    const flat = chartGeometry(
      days.map((d) => ({ ...d, total: 42 })),
      { width: 700, height: 200, left: 50, right: 10, top: 10, bottom: 20 },
    );
    expect(new Set(flat.points.map((p) => p.y)).size).toBe(1);
  });
  it('has no points for no days', () => {
    expect(
      chartGeometry([], { width: 700, height: 200, left: 50, right: 10, top: 10, bottom: 20 })
        .points,
    ).toEqual([]);
  });
});

describe('cardCurrency', () => {
  it('keeps the remembered currency while it answers', () => {
    expect(cardCurrency('KZT', false)).toBe('KZT');
  });
  it('falls back to USD when the remembered one has no rate', () => {
    expect(cardCurrency('RUB', true)).toBe('USD');
    expect(cardCurrency('USD', true)).toBe('USD');
  });
});

describe('rateDay', () => {
  it('names the day, and the year only when it differs', () => {
    expect(rateDay('2026-10-10', '2026-10-13')).toBe('Oct 10');
    expect(rateDay('2025-12-30', '2026-01-02')).toBe('Dec 30, 2025');
  });
});
