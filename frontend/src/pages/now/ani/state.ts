import { useCallback, useSyncExternalStore } from 'react';

/*
 * Ani's switches, remembered per browser: whether she is on Now at all (off
 * by default; the account menu turns her on), whether she is in her corner
 * or sent away, and her outfit.
 */

export type AniOutfit = 'p1' | 'p2' | 'lingerie' | 'bare';

interface AniState {
  on: boolean;
  here: boolean;
  outfit: AniOutfit;
}

const KEYS = { on: 'now:ani', here: 'now:ani:here', outfit: 'now:ani:outfit' } as const;

function read(key: string): string | null {
  try {
    return globalThis.localStorage?.getItem(key) ?? null;
  } catch {
    return null;
  }
}

function outfitOf(v: string | null): AniOutfit {
  return v === 'p1' || v === 'p2' || v === 'bare' ? v : 'lingerie';
}

function load(): AniState {
  return {
    on: read(KEYS.on) === '1',
    here: read(KEYS.here) !== '0',
    outfit: outfitOf(read(KEYS.outfit)),
  };
}

let state = load();
const listeners = new Set<() => void>();

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

function set(patch: Partial<AniState>) {
  state = { ...state, ...patch };
  try {
    globalThis.localStorage?.setItem(KEYS.on, state.on ? '1' : '0');
    globalThis.localStorage?.setItem(KEYS.here, state.here ? '1' : '0');
    globalThis.localStorage?.setItem(KEYS.outfit, state.outfit);
  } catch {
    // Remembering is a convenience; the switches still work.
  }
  for (const l of listeners) l();
}

/** One state for the page: the account menu and the corner agree. */
export function useAni(): [AniState, (patch: Partial<AniState>) => void] {
  const value = useSyncExternalStore(
    subscribe,
    () => state,
    () => state,
  );
  return [value, useCallback((patch: Partial<AniState>) => set(patch), [])];
}

/**
 * Where Ani's files live: next to the wallpapers in the public bucket, under
 * wallpapers/ani/ (the app's storage user may write only there and under
 * sounds/). Taken from a wallpaper's URL, so no host is configured twice;
 * null when there is no wallpaper to take it from.
 */
export function aniBase(wallpaperUrl: string | undefined): string | null {
  if (!wallpaperUrl) return null;
  const i = wallpaperUrl.indexOf('/wallpapers/');
  if (i < 0) return null;
  return `${wallpaperUrl.slice(0, i)}/wallpapers/ani/`;
}
