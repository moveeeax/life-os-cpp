/**
 * The browser's localStorage, or undefined when it is absent or blocked (a
 * locked down browser throws on the access itself; a render must not).
 */
export function browserStore(): Storage | undefined {
  try {
    return globalThis.localStorage ?? undefined;
  } catch {
    return undefined;
  }
}

/**
 * The JSON value kept under @p key, or @p fallback when it is absent, does
 * not parse or the storage is blocked. The value is not checked against T.
 */
export function readJson<T>(key: string, fallback: T): T {
  try {
    const raw = browserStore()?.getItem(key);
    return raw == null ? fallback : (JSON.parse(raw) as T);
  } catch {
    return fallback;
  }
}

/** Keeps @p value as JSON under @p key; a blocked or full storage is ignored. */
export function writeJson(key: string, value: unknown): void {
  try {
    browserStore()?.setItem(key, JSON.stringify(value));
  } catch {
    // Remembering is a convenience of this browser.
  }
}
