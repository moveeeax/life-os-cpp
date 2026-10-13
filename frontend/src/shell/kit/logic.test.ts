import { describe, expect, it } from 'vitest';

import { dialogWidth, formatAmount, splitTabs } from './logic';

describe('splitTabs', () => {
  const items = ['a', 'b', 'c', 'd', 'e', 'f', 'g'].map((x) => ({ to: `/${x}`, label: x }));
  it('keeps up to five as they are', () => {
    expect(splitTabs(items.slice(0, 5), 5)).toEqual({ shown: items.slice(0, 5), more: [] });
  });
  it('shows four and folds the rest when there are more than five', () => {
    const { shown, more } = splitTabs(items, 5);
    expect(shown.map((t) => t.label)).toEqual(['a', 'b', 'c', 'd']);
    expect(more.map((t) => t.label)).toEqual(['e', 'f', 'g']);
  });
});

describe('dialogWidth', () => {
  it('maps the old max-width classes', () => {
    expect(dialogWidth('max-w-sm')).toBe(384);
    expect(dialogWidth('max-w-lg')).toBe(512);
    expect(dialogWidth(undefined)).toBe(672);
    expect(dialogWidth('max-w-xl w-full')).toBe(576);
  });
});

describe('formatAmount', () => {
  it('signs and groups', () => {
    expect(formatAmount(-5000, { currency: 'KZT', digits: 0 })).toBe('−5,000 KZT');
    expect(formatAmount(460000, { currency: 'KZT', signed: true, digits: 0 })).toBe('+460,000 KZT');
    expect(formatAmount(12.5, { digits: 2 })).toBe('12.50');
  });
});
