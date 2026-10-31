/**
 * What stands on Ani's shelf, from data Now already has: a plant by the
 * longest habit streak, books by finished courses, cups by the week's kept
 * promises, a coffee cup by last night's sleep, a postcard of the last
 * country. Pure, so the picture is testable without a page.
 */

export type Plant = 'sprout' | 'bush' | 'bloom' | 'wilted';

/** The plant: a sprout under 7 days, a bush under 30, in bloom from 30; wilted the day a streak broke. */
export function plantOf(longest: number, brokeYesterday: boolean): Plant | null {
  if (brokeYesterday) return 'wilted';
  if (longest <= 0) return null;
  return longest < 7 ? 'sprout' : longest < 30 ? 'bush' : 'bloom';
}

/** Books on the shelf: one per finished course, at most five. */
export const booksOf = (finished: number) => Math.max(0, Math.min(5, finished));

/** Cups: one per promise of this week that is kept, at most three. */
export const cupsOf = (deals: readonly { done: number; target: number }[]) =>
  Math.min(3, deals.filter((d) => d.done >= d.target).length);

/** The morning coffee: full after six hours of sleep, empty under; none in the afternoon or without a night. */
export function coffeeOf(
  sleepMinutes: number | null | undefined,
  hour: number,
): 'full' | 'empty' | null {
  if (hour >= 12 || sleepMinutes == null || sleepMinutes <= 0) return null;
  return sleepMinutes >= 360 ? 'full' : 'empty';
}

/** A flag of a country code (two letters), as regional indicator letters. */
export function flagOf(code: string | null | undefined): string | null {
  if (!code || !/^[A-Z]{2}$/.test(code)) return null;
  return String.fromCodePoint(...[...code].map((c) => 0x1f1e6 + c.charCodeAt(0) - 65));
}
