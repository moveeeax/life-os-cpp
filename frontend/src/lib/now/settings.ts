import type { Chip } from './corners';
import type { NowCorners, NowLink, NowWeather } from './types';

/** Body top left, Goals top right, Food and money bottom left, Tasks bottom right. */
export const DEFAULT_CORNERS: NowCorners = { tl: 'body', tr: 'goals', bl: 'day', br: 'tasks' };

export const CORNER_KEYS = ['tl', 'tr', 'bl', 'br'] as const;
export type CornerKey = (typeof CORNER_KEYS)[number];

export const CORNER_LABEL: Record<CornerKey, string> = {
  tl: 'Top left',
  tr: 'Top right',
  bl: 'Bottom left',
  br: 'Bottom right',
};

export const CHIP_LABEL: Record<Chip['key'], string> = {
  body: 'Body',
  goals: 'Goals',
  day: 'Food and money',
  tasks: 'Tasks',
  habits: 'Habits',
};

/** A layout with every chip this client does not know turned into an empty corner. */
export function cleanCorners(corners: NowCorners): NowCorners {
  const known = (v: unknown) =>
    typeof v === 'string' && v in CHIP_LABEL ? (v as Chip['key']) : null;
  return {
    tl: known(corners.tl),
    tr: known(corners.tr),
    bl: known(corners.bl),
    br: known(corners.br),
  };
}

/** Whether @p key sits in a corner of the layout. */
export function isPlaced(corners: NowCorners, key: Chip['key']): boolean {
  return CORNER_KEYS.some((k) => corners[k] === key);
}

/**
 * The layout with @p key placed: as it is when already there, else in the first
 * empty corner, else in the bottom right (whatever sat there leaves Now).
 */
export function withChip(corners: NowCorners, key: Chip['key']): NowCorners {
  if (isPlaced(corners, key)) return corners;
  const empty = CORNER_KEYS.find((k) => corners[k] == null);
  return { ...corners, [empty ?? 'br']: key };
}

/** The layout with the corner of @p key emptied (its module is off); as it is when not placed. */
export function withoutChip(corners: NowCorners, key: Chip['key']): NowCorners {
  if (!isPlaced(corners, key)) return corners;
  const out = { ...corners };
  for (const k of CORNER_KEYS) if (out[k] === key) out[k] = null;
  return out;
}

/** The chips of the dock: Tasks lives in its own corner and is never offered. */
export const DOCK_CHIPS: Chip['key'][] = ['body', 'goals', 'day', 'habits'];

/** The four layout slots read as the order of the dock, top left first. */
export const DOCK_LABEL: Record<CornerKey, string> = {
  tl: 'Dock, first',
  tr: 'Dock, second',
  bl: 'Dock, third',
  br: 'Dock, fourth',
};

/** The dock tiles in slot order; a slot with Tasks, an unknown chip or nothing is skipped. */
export function dockChips(list: Chip[], corners: NowCorners): Chip[] {
  const out: Chip[] = [];
  for (const k of CORNER_KEYS) {
    const chip = list.find((c) => c.key === corners[k]);
    if (chip && chip.key !== 'tasks') out.push(chip);
  }
  return out;
}

export const LINKS_MAX = 8;

/**
 * A path of this app, an http(s) URL, or null for anything the page must not
 * link: a backslash reads as a slash in a browser and a control character or
 * a space is dropped by its URL parser, so none of them may appear anywhere.
 */
export function linkKind(url: string): 'path' | 'external' | null {
  // eslint-disable-next-line no-control-regex
  if (/[\\\x00-\x20\x7f]/.test(url)) return null;
  if (url.startsWith('/')) return url.startsWith('//') ? null : 'path';
  return /^https?:\/\//i.test(url) ? 'external' : null;
}

/** The links as the server keeps them: trimmed, no empty row, at most eight. */
export function cleanLinks(links: NowLink[]): NowLink[] {
  return links
    .map((l) => ({ title: l.title.trim(), url: l.url.trim() }))
    .filter((l) => l.title !== '' && l.url !== '')
    .slice(0, LINKS_MAX);
}

/** The chips that have a corner, each with the class of its corner, in the corner order. */
export function placeChips(list: Chip[], corners: NowCorners): { chip: Chip; spot: string }[] {
  const out: { chip: Chip; spot: string }[] = [];
  for (const corner of CORNER_KEYS) {
    const chip = list.find((c) => c.key === corners[corner]);
    if (chip) out.push({ chip, spot: `c-${corner}` });
  }
  return out;
}

/**
 * A text glyph for a WMO weather code, in the type of the page (U+FE0E asks
 * for the text form, not the colour emoji); the moon for a clear night.
 */
export function weatherGlyph(code: number, isDay: boolean): string {
  if (code === 0 || code === 1) return isDay ? '\u2600\uFE0E' : '\u263E';
  if (code === 2 || code === 3) return '\u2601\uFE0E';
  if (code === 45 || code === 48) return '\u224B';
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return '\u2602\uFE0E';
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return '\u2744\uFE0E';
  if (code >= 95 && code <= 99) return '\u03DF';
  return '';
}

/** The parts of the weather line: "Almaty", "10°", "fog". */
export function weatherLine(w: NowWeather): { place: string; degrees: string; words: string } {
  const t = Math.round(w.temperature);
  return { place: w.place, degrees: `${t === 0 ? 0 : t}°`, words: w.words };
}
