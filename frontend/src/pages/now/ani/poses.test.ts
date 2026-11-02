import { describe, expect, it } from 'vitest';

import { posesFor, RURUNE } from './poses';

describe('posesFor', () => {
  it('gives the whole Rurune set to the dress, the slip and the lingerie', () => {
    expect(RURUNE).toHaveLength(12);
    for (const o of ['p1', 'p2', 'lingerie'] as const) expect(posesFor(o)).toEqual(RURUNE);
  });

  it('gives bare every pose but the arms-behind-the-head one', () => {
    expect(posesFor('bare')).toEqual(RURUNE.filter((n) => n !== 'rurune_t2'));
    expect(posesFor('bare')).toHaveLength(11);
  });

  it('hands out copies', () => {
    posesFor('p1').pop();
    expect(posesFor('p1')).toEqual(RURUNE);
  });
});
