import { readJson, writeJson } from '@/lib/browserStore';

/*
 * Ani's marks in this browser: one JSON map per store key (now:ani:life,
 * now:ani:life2) from a line's key to what was kept for it, usually the day
 * it was said (YYYY-MM-DD), so each line comes once.
 */

/** Every mark of @p store; empty when none is kept or it does not read. */
export function readMarks(store: string): Record<string, string> {
  const v = readJson<unknown>(store, {});
  return v !== null && typeof v === 'object' && !Array.isArray(v)
    ? (v as Record<string, string>)
    : {};
}

/** What @p store keeps for @p key (usually the day it was said). */
export function seenOn(store: string, key: string): string | undefined {
  return readMarks(store)[key];
}

/** Keeps @p value for @p key in @p store; at worst a line comes once more. */
export function markOn(store: string, key: string, value: string): void {
  writeJson(store, { ...readMarks(store), [key]: value });
}
