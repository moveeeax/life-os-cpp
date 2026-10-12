import { describe, expect, it } from 'vitest';

import { greeting, partOfDay } from './greeting';

describe('partOfDay', () => {
  it('splits the day at 5, 12, 18 and 23', () => {
    expect(partOfDay(4)).toBe('night');
    expect(partOfDay(5)).toBe('morning');
    expect(partOfDay(11)).toBe('morning');
    expect(partOfDay(12)).toBe('afternoon');
    expect(partOfDay(18)).toBe('evening');
    expect(partOfDay(22)).toBe('evening');
    expect(partOfDay(23)).toBe('night');
    expect(partOfDay(0)).toBe('night');
  });
});

describe('greeting', () => {
  it('adds the first name when there is one', () => {
    expect(greeting(23, 'Ann')).toBe('Good night, Ann');
    expect(greeting(9, 'Ann')).toBe('Good morning, Ann');
  });
  it('leaves the comma out without a name', () => {
    expect(greeting(19, '')).toBe('Good evening');
    expect(greeting(13, null)).toBe('Good afternoon');
    expect(greeting(13, '   ')).toBe('Good afternoon');
  });
});
