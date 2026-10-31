/**
 * What Ani proposes for a thing dropped on her (lib/now/drag): pure choices
 * the corner turns into her line with buttons; nothing changes before a tap.
 */

/** The start (HH:MM on @p tz's clock) of the first free slot of 30 minutes or more still ahead. */
export function firstFreeSlot(
  free: readonly { start: string; end: string }[],
  now: number,
  tz: string,
): { at: string; start: number } | null {
  for (const f of free) {
    const start = Math.max(Date.parse(f.start), now);
    const end = Date.parse(f.end);
    if (end - start < 30 * 60_000) continue;
    // Rounded up to the next quarter of an hour.
    const q = Math.ceil(start / (15 * 60_000)) * 15 * 60_000;
    if (end - q < 30 * 60_000) continue;
    const at = new Intl.DateTimeFormat('en-GB', {
      timeZone: tz,
      hour: '2-digit',
      minute: '2-digit',
      hourCycle: 'h23',
    }).format(new Date(q));
    return { at, start: q };
  }
  return null;
}

/** The category most rows of the same merchant or name carry, with how many; null without one. */
export function topCategory(
  rows: readonly { category_id: string | null }[],
): { id: string; count: number } | null {
  const n = new Map<string, number>();
  for (const r of rows) if (r.category_id) n.set(r.category_id, (n.get(r.category_id) ?? 0) + 1);
  let best: { id: string; count: number } | null = null;
  for (const [id, count] of n) if (!best || count > best.count) best = { id, count };
  return best;
}

/** At most this many characters of dropped text go into a fact. */
export const FACT_MAX = 200;
