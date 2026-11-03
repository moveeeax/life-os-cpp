import type { Outfit } from './engine';

/*
 * The poses Ani may strike on her own, by outfit and by how the day goes.
 * The clips themselves (files, loading) are the engine's; this is only the
 * choice.
 */

/**
 * The owner's Rurune set, made for her own model (poses2/rurune_<n>.vmd,
 * one frame each). Every file is a clip the engine knows; only t2 plays
 * on its own (the owner's pick of 2026-11-03).
 */
export const RURUNE = ['rurune_t2'];
/**
 * Reward poses: c1 squats, z2 crawls, z4 lies, and from the packs p4_02,
 * p4_13 and p4_14 (on her back, legs up); played only when earned, never
 * by the idle picker.
 */
export const REWARD = ['rurune_c1', 'rurune_z2', 'rurune_z4', 'p4_02', 'p4_13', 'p4_14'];
/** Shelved, in the bucket and loadable, out of every pool. */
export const RURUNE_SHELVED = ['c2', 'c3', 'c4', 't1', 't3', 't4', 'z1', 'z3'].map(
  (n) => `rurune_${n}`,
);
/** Every Rurune clip the engine registers. */
export const RURUNE_ALL = [...RURUNE, 'rurune_c1', 'rurune_z2', 'rurune_z4', ...RURUNE_SHELVED];

/**
 * The owner's pick of 2026-11-03 from nine pose packs (poses3/<id>.vmd, one
 * frame each; p1 sab64 modeling, p2 おやすみ, p3 いろいろ2021, p4-p7 普通の
 * 撮影ポーズ 3-6, p8 Reggies, p9 Casual4), in pools by how the day goes.
 */
export const POOLS = {
  /** Standing at ease: the day's idle set. */
  calm: [
    'p1_04',
    'p1_05',
    'p1_06',
    'p1_12',
    'p1_17',
    'p3_01',
    'p3_04',
    'p3_08',
    'p3_10',
    'p5_01',
    'p5_02',
    'p5_05',
    'p6_07',
    'p6_08',
    'p6_09',
    'p6_10',
    'p7_07',
    'p7_09',
    'p8_05',
    'p9_01',
    ...RURUNE,
  ],
  /** Arms up, a kick, a flourish: a good mood or a task just closed. */
  bright: [
    'p1_09',
    'p1_16',
    'p1_22',
    'p3_03',
    'p3_05',
    'p3_06',
    'p3_07',
    'p5_06',
    'p5_09',
    'p5_21',
    'p5_25',
    'p7_05',
    'p7_06',
    'p7_08',
  ],
  /** Hugging her knees, slumped, curled up on her side: a cross day or late tasks. */
  low: ['p3_09', 'p3_21', 'p4_10', 'p4_15', 'p4_16', 'p5_08', 'p7_04'],
  /** Sitting on the ledge's edge: after a focus session and in the evening. */
  rest: ['p3_17', 'p4_09', 'p4_11', 'p4_12', 'p4_23', 'p4_24', 'p5_03'],
  /** Asleep, lying: the night. */
  night: [
    'p2_05',
    'p2_06',
    'p2_07',
    'p2_08',
    'p2_11',
    'p2_12',
    'p2_13',
    'p2_14',
    'p2_15',
    'p2_16',
    'p3_18',
    'p4_01',
    'p4_03',
    'p4_04',
    'p4_06',
  ],
} as const;
export type Pool = keyof typeof POOLS;

/**
 * Her yaw while a lying pose plays, so she lies along the ledge, head by
 * its edge (the engine's `turn`): by the body axis in the file, +z turns
 * +π/2, -z turns -π/2, along x turns round.
 */
export const TURN: Record<string, number> = {};
for (const n of [
  'p2_05',
  'p2_06',
  'p2_07',
  'p2_08',
  'p2_11',
  'p2_12',
  'p2_13',
  'p2_14',
  'p2_15',
  'p2_16',
])
  TURN[n] = Math.PI / 2;
for (const n of ['p4_01', 'p4_02', 'p4_03', 'p4_04', 'p4_06', 'p4_13', 'p4_14', 'p4_15', 'p4_16'])
  TURN[n] = -Math.PI / 2;
for (const n of ['p3_18', 'p3_21']) TURN[n] = Math.PI;

/** Every pack pose the engine registers (poses3/<id>.vmd). */
export const PACK_ALL = [
  ...new Set([...Object.values(POOLS).flat(), ...REWARD].filter((n) => /^p\d_\d\d$/.test(n))),
];

/** How the day goes, for the choice of a pool. */
export interface PoseContext {
  /** Her mood of the day (mood.ts). */
  mood: -1 | 0 | 1;
  /** The hour in the user's zone. */
  hour: number;
  /** Seconds since a focus session ran to its end; Infinity when none did. */
  sinceFocus: number;
  /** Seconds since a task closed; Infinity when none did. */
  sinceTask: number;
  /** Tasks past their day. */
  late: boolean;
}

/** As the day goes when nothing is known: the calm pool. */
export const PLAIN_DAY: PoseContext = {
  mood: 0,
  hour: 12,
  sinceFocus: Infinity,
  sinceTask: Infinity,
  late: false,
};

/** After a focus session she rests this long (s). */
export const REST_AFTER_FOCUS_S = 20 * 60;
/** A closed task brightens her poses this long (s). */
export const BRIGHT_AFTER_TASK_S = 10 * 60;
/** Her night, local hours: from NIGHT_FROM up to NIGHT_TO. */
export const NIGHT_FROM = 23;
export const NIGHT_TO = 6;

/** The pool of the moment. Bare she does not lie down for the night (she gets cold): she rests. */
export function poolFor(outfit: Outfit, ctx: PoseContext): Pool {
  const night = ctx.hour >= NIGHT_FROM || ctx.hour < NIGHT_TO;
  if (night) return outfit === 'bare' ? 'rest' : 'night';
  if (ctx.sinceFocus < REST_AFTER_FOCUS_S) return 'rest';
  if (ctx.sinceTask < BRIGHT_AFTER_TASK_S || ctx.mood === 1) return 'bright';
  if (ctx.mood === -1 || ctx.late) return 'low';
  return 'calm';
}

/** The poses she may strike on her own now (a copy). */
export function posesFor(outfit: Outfit, ctx: PoseContext = PLAIN_DAY): string[] {
  return [...POOLS[poolFor(outfit, ctx)]];
}
