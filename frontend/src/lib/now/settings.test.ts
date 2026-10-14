import { describe, expect, it } from 'vitest';

import type { Chip } from './corners';
import { DEFAULT_CORNERS, cleanCorners, placeChips, weatherGlyph, weatherLine } from './settings';

const chip = (key: Chip['key']): Chip => ({ key, title: key, main: [], small: [], dot: null });
const all = [chip('body'), chip('goals'), chip('day'), chip('tasks')];

describe('placeChips', () => {
  it('puts each chip in its corner, in the corner order', () => {
    expect(placeChips(all, DEFAULT_CORNERS).map((p) => `${p.chip.key}@${p.spot}`)).toEqual([
      'body@c-tl',
      'goals@c-tr',
      'day@c-bl',
      'tasks@c-br',
    ]);
  });
  it('moves chips and leaves a hidden one out', () => {
    const placed = placeChips(all, { tl: 'tasks', tr: null, bl: 'day', br: 'body' });
    expect(placed.map((p) => `${p.chip.key}@${p.spot}`)).toEqual([
      'tasks@c-tl',
      'day@c-bl',
      'body@c-br',
    ]);
  });
});

describe('weatherLine', () => {
  it('reads the place, the rounded temperature and the words', () => {
    expect(
      weatherLine({
        place: 'Almaty',
        temperature: 9.6,
        min: 9,
        max: 19.2,
        code: 45,
        words: 'fog',
        is_day: false,
        time: '2026-10-14T06:00',
      }),
    ).toEqual({ place: 'Almaty', degrees: '10°', words: 'fog' });
  });
  it('shows no minus zero', () => {
    const w = {
      place: 'X',
      temperature: -0.3,
      min: null,
      max: null,
      code: 0,
      words: 'clear',
      is_day: true,
      time: '',
    };
    expect(weatherLine(w).degrees).toBe('0°');
  });
});

describe('cleanCorners', () => {
  it('turns a chip this client does not know into an empty corner', () => {
    expect(
      cleanCorners({
        tl: 'body',
        tr: 'weather',
        bl: 'day',
        br: 'tasks',
      } as unknown as typeof DEFAULT_CORNERS),
    ).toEqual({ tl: 'body', tr: null, bl: 'day', br: 'tasks' });
  });
  it('keeps a known layout as it is', () => {
    expect(cleanCorners(DEFAULT_CORNERS)).toEqual(DEFAULT_CORNERS);
  });
});

describe('weatherGlyph', () => {
  it('draws the weather in text glyphs, the moon at a clear night', () => {
    expect(weatherGlyph(0, true)).toBe('☀\uFE0E');
    expect(weatherGlyph(1, false)).toBe('☾');
    expect(weatherGlyph(3, true)).toBe('☁\uFE0E');
    expect(weatherGlyph(45, true)).toBe('≋');
    expect(weatherGlyph(63, true)).toBe('☂\uFE0E');
    expect(weatherGlyph(81, false)).toBe('☂\uFE0E');
    expect(weatherGlyph(73, true)).toBe('❄\uFE0E');
    expect(weatherGlyph(95, true)).toBe('ϟ');
    expect(weatherGlyph(42, true)).toBe('');
  });
});
