import { describe, expect, it } from 'vitest';

import { posesFor, RURUNE } from './poses';

describe('posesFor', () => {
  it('gives the whole Rurune set to the dress, the slip and the lingerie', () => {
    expect(RURUNE).toHaveLength(12);
    for (const o of ['p1', 'p2', 'lingerie'] as const) expect(posesFor(o)).toEqual(RURUNE);
  });

  it('keeps six of them for bare: no arms behind the head, no floor', () => {
    expect(posesFor('bare')).toEqual([
      'rurune_c1',
      'rurune_c3',
      'rurune_c4',
      'rurune_t1',
      'rurune_t3',
      'rurune_t4',
    ]);
  });

  it('hands out copies', () => {
    posesFor('p1').pop();
    expect(posesFor('p1')).toEqual(RURUNE);
  });
});
