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

/** The chips that have a corner, each with the class of its corner, in the corner order. */
export function placeChips(list: Chip[], corners: NowCorners): { chip: Chip; spot: string }[] {
  const out: { chip: Chip; spot: string }[] = [];
  for (const corner of CORNER_KEYS) {
    const chip = list.find((c) => c.key === corners[corner]);
    if (chip) out.push({ chip, spot: `c-${corner}` });
  }
  return out;
}

/** The parts of the weather line: "Almaty", "10°", "fog". */
export function weatherLine(w: NowWeather): { place: string; degrees: string; words: string } {
  const t = Math.round(w.temperature);
  return { place: w.place, degrees: `${t === 0 ? 0 : t}°`, words: w.words };
}
