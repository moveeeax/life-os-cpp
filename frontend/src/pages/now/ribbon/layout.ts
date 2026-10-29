import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { components } from '@/lib/api/schema.gen';

export type NowDay = components['schemas']['NowDay'];

/** The ribbon's hours: 08:00 to 23:00 local. */
export const RIBBON_FROM = 8;
export const RIBBON_TO = 23;
/** The next event's line turns red this many minutes before it. */
export const SOON_MINUTES = 10;

export type BlockKind = 'meet' | 'did' | 'gym' | 'free';

export interface Block {
  kind: BlockKind;
  key: string;
  title: string;
  /** Position on the ribbon, 0 to 100. */
  left: number;
  width: number;
  start: number;
  end: number;
  /** A calendar event's id, for pointing at it. */
  id?: string;
}

export interface Pin {
  key: string;
  title: string;
  left: number;
  done: boolean;
}

/** Minutes past local midnight of @p ms in @p tz. */
export function localMinutes(ms: number, tz: string): number {
  const parts = new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  }).formatToParts(new Date(ms));
  const h = Number(parts.find((p) => p.type === 'hour')?.value ?? 0);
  const m = Number(parts.find((p) => p.type === 'minute')?.value ?? 0);
  return h * 60 + m;
}

/** The ribbon's 0..100 of local minutes; outside the hours it is clipped. */
export function pctOf(minutes: number): number {
  const v = ((minutes - RIBBON_FROM * 60) / ((RIBBON_TO - RIBBON_FROM) * 60)) * 100;
  return Math.min(100, Math.max(0, v));
}

/** The local day (YYYY-MM-DD) of @p ms in @p tz. */
export function localDay(ms: number, tz: string): string {
  return new Intl.DateTimeFormat('en-CA', { timeZone: tz }).format(new Date(ms));
}

function span(
  kind: BlockKind,
  key: string,
  title: string,
  a: number,
  b: number,
  date: string,
  tz: string,
  id?: string,
): Block | null {
  // Minutes of the day; a side on another day sits at the ribbon's edge.
  const am = localDay(a, tz) < date ? 0 : localMinutes(a, tz);
  const bm = localDay(b, tz) > date ? 24 * 60 : localMinutes(b, tz);
  const left = pctOf(am);
  const right = pctOf(bm);
  if (right <= left) return null;
  return { kind, key, title, left, width: right - left, start: a, end: b, id };
}

/** The day's blocks: free slots under, then events, focus done and workouts (one still running ends now). */
export function blocksOf(
  day: NowDay | undefined,
  tz: string,
  now: number,
  /** The start of the focus running now, drawn up to `now`. */
  focusFrom: number | null = null,
): Block[] {
  if (!day) return [];
  const out: Block[] = [];
  const date = day.date;
  for (const f of day.free) {
    const b = span(
      'free',
      `free:${f.start}`,
      'Free',
      Date.parse(f.start),
      Date.parse(f.end),
      date,
      tz,
    );
    if (b) out.push(b);
  }
  for (const e of day.events) {
    if (e.all_day) continue;
    const b = span(
      'meet',
      `ev:${e.id}`,
      e.title,
      Date.parse(e.start),
      Date.parse(e.end),
      date,
      tz,
      e.id,
    );
    if (b) out.push(b);
  }
  for (const f of day.focus) {
    if (!f.start || !f.end) continue;
    const b = span(
      'did',
      `focus:${f.start}`,
      f.label || 'Focus',
      Date.parse(f.start),
      Date.parse(f.end),
      date,
      tz,
    );
    if (b) out.push(b);
  }
  for (const w of day.workouts) {
    if (!w.start) continue;
    const end = w.end ? Date.parse(w.end) : now;
    const b = span(
      'gym',
      `gym:${w.start}`,
      w.name || 'Workout',
      Date.parse(w.start),
      end,
      date,
      tz,
    );
    if (b) out.push(b);
  }
  if (focusFrom && focusFrom < now) {
    const b = span('did', 'focus:running', 'Focus', focusFrom, now, date, tz);
    if (b) out.push(b);
  }
  return out;
}

/** Tasks with a time, as pins on the ribbon. */
export function pinsOf(day: NowDay | undefined): Pin[] {
  return (day?.tasks ?? [])
    .filter((t) => t.time && t.id)
    .map((t) => {
      const [h, m] = (t.time ?? '00:00').split(':').map(Number);
      return {
        key: `task:${t.id}`,
        title: t.title ?? '',
        left: pctOf(h * 60 + m),
        done: t.status === 'done',
      };
    });
}

/** The next timed event (or the one going on): its title and minutes to its start, 0 while it runs. */
export function nextEvent(
  day: NowDay | undefined,
  now: number,
): { title: string; minutes: number; id: string } | null {
  for (const e of day?.events ?? []) {
    if (e.all_day) continue;
    const a = Date.parse(e.start);
    const b = Date.parse(e.end);
    if (b <= now) continue;
    return { title: e.title, minutes: Math.max(0, Math.round((a - now) / 60_000)), id: e.id };
  }
  return null;
}

/** "in 25 min · Call", "now · Call", "at 14:00 · Call" past an hour. */
export function nextWords(
  n: { title: string; minutes: number } | null,
  startLocal: string,
): string | null {
  if (!n) return null;
  if (n.minutes === 0) return `now · ${n.title}`;
  if (n.minutes < 60) return `in ${n.minutes} min · ${n.title}`;
  return `at ${startLocal} · ${n.title}`;
}

/** The free slot at a ribbon position (0..100), if any. */
export function freeAt(blocks: Block[], pct: number): Block | null {
  return blocks.find((b) => b.kind === 'free' && pct >= b.left && pct <= b.left + b.width) ?? null;
}

/** Ani points at a block of the ribbon with this event (detail: the calendar event's id). */
export const RIBBON_POINT = 'now:ribbon:point';

/** HH:MM of @p ms in @p tz. */
export const hhmm = (ms: number, tz: string) =>
  new Intl.DateTimeFormat('en-GB', {
    timeZone: tz,
    hour: '2-digit',
    minute: '2-digit',
    hourCycle: 'h23',
  }).format(new Date(ms));

export function useNowDay(date: string, tz: string) {
  return useQuery({
    queryKey: qk.now.day(date, tz),
    staleTime: 60_000,
    refetchInterval: 5 * 60_000,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: NowDay }>('/api/v1/now/day', { query: { date, tz }, signal }))
        .data,
  });
}
