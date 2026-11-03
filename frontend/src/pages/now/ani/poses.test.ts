import { describe, expect, it } from 'vitest';

import { posesFor, REWARD, RURUNE, RURUNE_ALL } from './poses';

describe('posesFor', () => {
  it('plays t2 alone on her own, dressed or bare', () => {
    for (const o of ['p1', 'p2', 'lingerie', 'bare'] as const)
      expect(posesFor(o)).toEqual(['rurune_t2']);
  });

  it('keeps the reward and shelved poses out of the idle pool but registered', () => {
    expect(REWARD).toEqual(['rurune_c1', 'rurune_z2', 'rurune_z4']);
    expect(RURUNE_ALL).toHaveLength(12);
    expect(new Set(RURUNE_ALL).size).toBe(12);
  });

  it('hands out copies', () => {
    posesFor('p1').pop();
    expect(posesFor('p1')).toEqual(RURUNE);
  });
});
