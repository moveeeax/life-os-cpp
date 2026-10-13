import { describe, expect, it } from 'vitest';

import { matchesPrefix } from './calm';

describe('matchesPrefix', () => {
  const prefixes = ['/money'];
  it('takes the section and its pages', () => {
    expect(matchesPrefix('/money', prefixes)).toBe(true);
    expect(matchesPrefix('/money/summary', prefixes)).toBe(true);
  });
  it('leaves other paths out', () => {
    expect(matchesPrefix('/moneyx', prefixes)).toBe(false);
    expect(matchesPrefix('/', prefixes)).toBe(false);
    expect(matchesPrefix('/food', prefixes)).toBe(false);
  });
});
