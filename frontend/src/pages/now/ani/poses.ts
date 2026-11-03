import type { Outfit } from './engine';

/*
 * The poses Ani may strike on her own, by outfit. The clips themselves
 * (files, loading) are the engine's; this is only the choice.
 */

/**
 * The owner's Rurune set, made for her own model (poses2/rurune_<n>.vmd,
 * one frame each). Every file is a clip the engine knows; only these play
 * on their own (the owner's pick of 2026-11-03).
 */
export const RURUNE = ['rurune_t2'];
/** Reward poses: c1 squats, z2 crawls, z4 lies; played only when earned, never by the idle picker. */
export const REWARD = ['rurune_c1', 'rurune_z2', 'rurune_z4'];
/** Shelved, in the bucket and loadable, out of every pool. */
export const RURUNE_SHELVED = ['c2', 'c3', 'c4', 't1', 't3', 't4', 'z1', 'z3'].map(
  (n) => `rurune_${n}`,
);
/** Every Rurune clip the engine registers. */
export const RURUNE_ALL = [...RURUNE, ...REWARD, ...RURUNE_SHELVED];

/** The poses she may strike in this outfit on her own: the same set bare and dressed. */
export function posesFor(outfit: Outfit): string[] {
  void outfit;
  return [...RURUNE];
}
