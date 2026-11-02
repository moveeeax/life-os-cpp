import type { Outfit } from './engine';

/*
 * The poses Ani may strike on her own, by outfit. The clips themselves
 * (files, loading) are the engine's; this is only the choice.
 */

/**
 * The owner's Rurune set, made for her own model (poses2/rurune_<n>.vmd,
 * one frame each): t1 to t4 stand, c1 to c4 sit or squat with the knees
 * apart, z1 to z4 lie on the floor.
 */
export const RURUNE = ['c1', 'c2', 'c3', 'c4', 't1', 't2', 't3', 't4', 'z1', 'z2', 'z3', 'z4'].map(
  (n) => `rurune_${n}`,
);

// Bare keeps the standing poses t1, t3 and t4: the sitting and lying ones
// have the knees apart, and t2 is the arms-behind-the-head one.
const DRESSED_ONLY = new Set([
  'rurune_c1',
  'rurune_c2',
  'rurune_c3',
  'rurune_c4',
  'rurune_t2',
  'rurune_z1',
  'rurune_z2',
  'rurune_z3',
  'rurune_z4',
]);

/** The poses she may strike in this outfit. */
export function posesFor(outfit: Outfit): string[] {
  return outfit === 'bare' ? RURUNE.filter((n) => !DRESSED_ONLY.has(n)) : [...RURUNE];
}
