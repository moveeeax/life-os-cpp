/** Pinned to one mode, "off", or empty for by itself (localStorage). */
export const MODE_KEY = 'now:mode';

/** The browser's pin of the Now mode cards; null when none is kept or storage is blocked. */
export function modePin(): string | null {
  try {
    return localStorage.getItem(MODE_KEY);
  } catch {
    return null;
  }
}

/** Keeps @p pin in this browser, or forgets it on null; a blocked storage is ignored. */
export function setModePin(pin: string | null): void {
  try {
    if (pin === null) localStorage.removeItem(MODE_KEY);
    else localStorage.setItem(MODE_KEY, pin);
  } catch {
    // Remembering it is a convenience.
  }
}
