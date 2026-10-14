// The last answers of Now kept in the browser, so a new tab paints at once.
// Every access is guarded: storage can be blocked, full or absent.
import { todayLocal } from '@/lib/dates';

export interface KeyValueStore {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

/** A store whose keys can be walked and removed (localStorage). */
export interface ListableStore extends KeyValueStore {
  readonly length: number;
  key(index: number): string | null;
  removeItem(key: string): void;
}

function browserStore(): ListableStore | undefined {
  try {
    return globalThis.localStorage ?? undefined;
  } catch {
    return undefined;
  }
}

/** One entry per user and local date: yesterday's digest never shows as today's. */
export function cacheKey(userId: string, date: string): string {
  return `now:${userId}:${date}`;
}

const DAY_KEY = /^now:([^:]+):(\d{4}-\d{2}-\d{2})$/;

/** Every key of @p store whose name matches @p test; [] when it cannot be read. */
export function keysWhere(
  test: (key: string) => boolean,
  store: ListableStore | undefined = browserStore(),
): string[] {
  const out: string[] = [];
  try {
    if (!store) return out;
    for (let i = 0; i < store.length; i++) {
      const k = store.key(i);
      if (k !== null && test(k)) out.push(k);
    }
  } catch {
    // A blocked storage has nothing to walk.
  }
  return out;
}

/** Drops the older day digests of @p userId, so only @p keepDate stays in the browser. */
export function pruneDays(
  userId: string,
  keepDate: string,
  store: ListableStore | undefined = browserStore(),
): void {
  const stale = keysWhere((k) => {
    const m = DAY_KEY.exec(k);
    return m !== null && m[1] === userId && m[2] !== keepDate;
  }, store);
  try {
    for (const k of stale) store?.removeItem(k);
  } catch {
    // Nothing to do: a stale entry only costs space.
  }
}

export function readCache<T>(
  key: string,
  store: KeyValueStore | undefined = browserStore(),
): T | null {
  try {
    const raw = store?.getItem(key);
    return raw ? (JSON.parse(raw) as T) : null;
  } catch {
    return null;
  }
}

export function writeCache<T>(
  key: string,
  value: T,
  store: KeyValueStore | undefined = browserStore(),
): void {
  try {
    store?.setItem(key, JSON.stringify(value));
  } catch {
    // A full or blocked storage only costs the instant paint.
  }
}

/** YYYY-MM-DD of @p d in the zone @p tz. */
export const localDate = (d: Date, tz: string): string => todayLocal(tz, d);
