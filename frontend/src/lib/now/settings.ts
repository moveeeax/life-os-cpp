import type { Chip } from './corners';
import type { NowCorners, NowWeather } from './types';

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
