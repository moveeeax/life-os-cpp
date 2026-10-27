// Which wallpaper a screen shows: a random one of the device's set, a still
// picture instead of a video when motion is reduced or data saved, and never
// a video on a phone.
import type { Wallpaper } from './types';

export type Device = 'desktop' | 'phone';

export interface Pick {
  kind: 'video' | 'image' | 'none';
  src: string | null;
  poster: string | null;
  /** The wallpaper it came from. */
  id?: string;
}

/** Ani's webhook or MCP asks the backdrop for another wallpaper (detail: {which?}). */
export const ANI_WALLPAPER_EVENT = 'ani:wallpaper';

export const PHONE_MAX_WIDTH = 768;

export function deviceOf(width: number): Device {
  return width <= PHONE_MAX_WIDTH ? 'phone' : 'desktop';
}

const NONE: Pick = { kind: 'none', src: null, poster: null };

export function pickWallpaper(
  list: Wallpaper[],
  device: Device,
  opts: { reducedMotion: boolean; saveData: boolean },
  rnd: () => number = Math.random,
): Pick {
  // A phone takes images only, whatever list it was handed.
  const usable = device === 'phone' ? list.filter((w) => w.kind === 'image') : list;
  if (usable.length === 0) return NONE;
  const w = usable[Math.min(usable.length - 1, Math.floor(rnd() * usable.length))];
  return show(w, opts);
}

function show(w: Wallpaper, opts: { reducedMotion: boolean; saveData: boolean }): Pick {
  if (w.kind === 'image') return { kind: 'image', src: w.url, poster: null, id: w.id };
  if (opts.reducedMotion || opts.saveData) {
    return w.poster_url ? { kind: 'image', src: w.poster_url, poster: null, id: w.id } : NONE;
  }
  return { kind: 'video', src: w.url, poster: w.poster_url, id: w.id };
}

/**
 * The wallpaper to switch to: @p which when it is in the set, else (none
 * asked) the one after @p current; an unknown id keeps the current one.
 */
export function switchWallpaper(
  list: Wallpaper[],
  device: Device,
  opts: { reducedMotion: boolean; saveData: boolean },
  current: string | null,
  which?: string,
): Pick {
  const usable = device === 'phone' ? list.filter((w) => w.kind === 'image') : list;
  if (usable.length === 0) return NONE;
  const at = usable.findIndex((w) => w.id === current);
  if (which !== undefined) {
    const asked = usable.find((w) => w.id === which);
    return show(asked ?? usable[Math.max(0, at)], opts);
  }
  return show(usable[(at + 1) % usable.length], opts);
}
