import { describe, expect, it } from 'vitest';

import {
  PACK_ALL,
  PLAIN_DAY,
  POOLS,
  poolFor,
  posesFor,
  REWARD,
  RURUNE,
  RURUNE_ALL,
  TURN,
} from './poses';

describe('poolFor', () => {
  it('is calm on a plain day, dressed or bare', () => {
    for (const o of ['p1', 'p2', 'lingerie', 'bare'] as const)
      expect(poolFor(o, PLAIN_DAY)).toBe('calm');
  });

  it('follows the mood and the late tasks', () => {
    expect(poolFor('p1', { ...PLAIN_DAY, mood: 1 })).toBe('bright');
    expect(poolFor('p1', { ...PLAIN_DAY, mood: -1 })).toBe('low');
    expect(poolFor('p1', { ...PLAIN_DAY, late: true })).toBe('low');
    expect(poolFor('p1', { ...PLAIN_DAY, late: true, mood: 1 })).toBe('bright');
  });

  it('brightens for ten minutes after a task closes', () => {
    expect(poolFor('p1', { ...PLAIN_DAY, mood: -1, sinceTask: 599 })).toBe('bright');
    expect(poolFor('p1', { ...PLAIN_DAY, mood: -1, sinceTask: 601 })).toBe('low');
  });

  it('rests for twenty minutes after a focus session, whatever the mood', () => {
    expect(poolFor('p1', { ...PLAIN_DAY, mood: 1, sinceFocus: 1199 })).toBe('rest');
    expect(poolFor('p1', { ...PLAIN_DAY, mood: 1, sinceFocus: 1201 })).toBe('bright');
  });

  it('sleeps at night, dressed; bare she rests instead', () => {
    for (const hour of [23, 0, 5]) {
      expect(poolFor('p1', { ...PLAIN_DAY, hour, sinceFocus: 10 })).toBe('night');
      expect(poolFor('bare', { ...PLAIN_DAY, hour })).toBe('rest');
    }
    expect(poolFor('p1', { ...PLAIN_DAY, hour: 6 })).toBe('calm');
    expect(poolFor('p1', { ...PLAIN_DAY, hour: 22 })).toBe('calm');
  });
});

describe('posesFor', () => {
  it('hands out a copy of the pool of the moment', () => {
    expect(posesFor('p1')).toEqual([...POOLS.calm]);
    posesFor('p1').pop();
    expect(posesFor('p1')).toEqual([...POOLS.calm]);
    expect(posesFor('p1', { ...PLAIN_DAY, hour: 2 })).toEqual([...POOLS.night]);
  });

  it('keeps t2 in the calm pool', () => {
    expect(RURUNE).toEqual(['rurune_t2']);
    expect(POOLS.calm).toContain('rurune_t2');
  });
});

describe('the sets', () => {
  it('keeps the reward and shelved poses out of every idle pool', () => {
    const idle = new Set(Object.values(POOLS).flat());
    for (const n of [...REWARD, ...RURUNE_ALL.filter((x) => x !== 'rurune_t2')])
      expect(idle.has(n)).toBe(false);
    expect(REWARD).toEqual(['rurune_c1', 'rurune_z2', 'rurune_z4', 'p4_02', 'p4_13', 'p4_14']);
    expect(RURUNE_ALL).toHaveLength(12);
  });

  it('registers every pack pose once, 66 of them, and the turns name registered ones', () => {
    expect(PACK_ALL).toHaveLength(66);
    expect(new Set(PACK_ALL).size).toBe(66);
    for (const n of Object.keys(TURN)) expect(PACK_ALL).toContain(n);
    for (const n of POOLS.night) expect(TURN[n]).toBeDefined();
  });
});
