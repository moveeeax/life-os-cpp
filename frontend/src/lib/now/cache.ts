// The last answers of Now kept in the browser, so a new tab paints at once.
// Every access is guarded: storage can be blocked, full or absent.

export interface KeyValueStore {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

function browserStore(): KeyValueStore | undefined {
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
export function localDate(d: Date, tz: string): string {
  const parts = new Intl.DateTimeFormat('en-CA', {
    timeZone: tz,
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
  }).formatToParts(d);
  const get = (t: string) => parts.find((p) => p.type === t)?.value ?? '';
  return `${get('year')}-${get('month')}-${get('day')}`;
}
