/*
 * Her mood of the day in this browser: a score that kind taps lift and rude
 * ones lower (touch.ts), kept between -10 and 10 and started afresh each
 * day. Above MOOD_HIGH she smiles more and missed the user; below MOOD_LOW
 * she is a little cross and greets with a "Хм".
 */

const KEY = 'now:ani:mood';
const LIMIT = 10;
export const MOOD_HIGH = 4;
export const MOOD_LOW = -3;

interface Stored {
  day: string;
  score: number;
}

function read(day: string): number {
  try {
    const s = JSON.parse(localStorage.getItem(KEY) ?? 'null') as Stored | null;
    return s && s.day === day && Number.isFinite(s.score) ? s.score : 0;
  } catch {
    return 0;
  }
}

/** Today's mood score. */
export function moodOf(day: string): number {
  return read(day);
}

/** Moves today's mood by @p delta; the new score. */
export function moveMood(day: string, delta: number): number {
  const score = Math.max(-LIMIT, Math.min(LIMIT, read(day) + delta));
  try {
    localStorage.setItem(KEY, JSON.stringify({ day, score } satisfies Stored));
  } catch {
    // Private mode: the mood lasts for this page.
  }
  return score;
}

/** The mood as the engine takes it: -1 cross, 0 as usual, 1 happy. */
export function moodLevel(score: number): -1 | 0 | 1 {
  return score >= MOOD_HIGH ? 1 : score <= MOOD_LOW ? -1 : 0;
}
