import { describe, expect, it } from 'vitest';

import { booksOf, coffeeOf, cupsOf, flagOf, plantOf } from './shelfItems';

describe('her shelf', () => {
  it('HerShelf: the plant grows with the streak and wilts the day it breaks', () => {
    expect(plantOf(0, false)).toBeNull();
    expect(plantOf(3, false)).toBe('sprout');
    expect(plantOf(12, false)).toBe('bush');
    expect(plantOf(45, false)).toBe('bloom');
    expect(plantOf(45, true)).toBe('wilted');
  });

  it('books, cups, coffee and the postcard', () => {
    expect(booksOf(7)).toBe(5);
    expect(
      cupsOf([
        { done: 3, target: 3 },
        { done: 1, target: 3 },
      ]),
    ).toBe(1);
    expect(coffeeOf(400, 8)).toBe('full');
    expect(coffeeOf(300, 8)).toBe('empty');
    expect(coffeeOf(400, 14)).toBeNull();
    expect(flagOf('TH')).toBe('🇹🇭');
    expect(flagOf('x')).toBeNull();
  });
});
