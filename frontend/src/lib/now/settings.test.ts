import { describe, expect, it } from 'vitest';

import type { Chip } from './corners';
import {
  DEFAULT_CORNERS,
  cleanCorners,
  cleanLinks,
  dockChips,
  isPlaced,
  linkKind,
  placeChips,
  reorderDock,
  weatherGlyph,
  weatherLine,
  withChip,
  withoutChip,
} from './settings';

const chip = (key: Chip['key']): Chip => ({ key, title: key, main: [], small: [], dot: null });
const all = [chip('body'), chip('goals'), chip('day'), chip('tasks'), chip('habits')];

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

describe('the Habits plate', () => {
  it('is not placed by default', () => {
    expect(isPlaced(DEFAULT_CORNERS, 'habits')).toBe(false);
    expect(placeChips(all, DEFAULT_CORNERS).map((p) => p.chip.key)).not.toContain('habits');
  });
  it('takes the bottom right when every corner is full, and Tasks leaves Now', () => {
    const next = withChip(DEFAULT_CORNERS, 'habits');
    expect(next).toEqual({ tl: 'body', tr: 'goals', bl: 'day', br: 'habits' });
    expect(placeChips(all, next).map((p) => `${p.chip.key}@${p.spot}`)).toEqual([
      'body@c-tl',
      'goals@c-tr',
      'day@c-bl',
      'habits@c-br',
    ]);
  });
  it('takes the first empty corner, and stays where it is once placed', () => {
    expect(withChip({ tl: 'body', tr: null, bl: 'day', br: 'tasks' }, 'habits').tr).toBe('habits');
    const placed = { tl: 'habits', tr: null, bl: null, br: null } as const;
    expect(withChip(placed, 'habits')).toBe(placed);
  });
  it('empties the corner of a chip whose module is off', () => {
    const placed = { tl: 'body', tr: 'habits', bl: 'day', br: 'tasks' } as const;
    expect(withoutChip(placed, 'habits')).toEqual({ tl: 'body', tr: null, bl: 'day', br: 'tasks' });
    expect(withoutChip(DEFAULT_CORNERS, 'habits')).toBe(DEFAULT_CORNERS);
  });
  it('is a chip this client knows', () => {
    const layout = { tl: 'body', tr: 'goals', bl: 'day', br: 'habits' } as const;
    expect(cleanCorners(layout)).toEqual(layout);
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

describe('dockChips', () => {
  it('orders the tiles by slot and never shows Tasks', () => {
    expect(dockChips(all, DEFAULT_CORNERS).map((c) => c.key)).toEqual(['body', 'goals', 'day']);
    expect(
      dockChips(all, { tl: 'habits', tr: 'tasks', bl: null, br: 'body' }).map((c) => c.key),
    ).toEqual(['habits', 'body']);
  });
});

describe('linkKind', () => {
  it('tells a path from an external URL and refuses the rest', () => {
    expect(linkKind('/money/summary')).toBe('path');
    expect(linkKind('https://bank.example')).toBe('external');
    expect(linkKind('http://x')).toBe('external');
    expect(linkKind('HTTPS://Bank.example/')).toBe('external');
    expect(linkKind('//evil.example')).toBeNull();
    expect(linkKind('javascript:alert(1)')).toBeNull();
    expect(linkKind('money')).toBeNull();
    expect(linkKind('/\\evil.example')).toBeNull();
    expect(linkKind('/\t/evil.example')).toBeNull();
    expect(linkKind('https://a.example/a b')).toBeNull();
  });
});

describe('cleanLinks', () => {
  it('trims, drops empty rows and keeps eight', () => {
    const nine = Array.from({ length: 9 }, (_, i) => ({ title: `t${i}`, url: `/p${i}` }));
    expect(cleanLinks(nine)).toHaveLength(8);
    expect(
      cleanLinks([
        { title: '  A ', url: ' /a ' },
        { title: '', url: '/b' },
        { title: 'C', url: ' ' },
      ]),
    ).toEqual([{ title: 'A', url: '/a' }]);
  });
});

describe('reorderDock', () => {
  it('moves the chip at a dock position to another, shifting the rest', () => {
    const c = { tl: 'body', tr: 'goals', bl: 'day', br: 'habits' } as const;
    expect(reorderDock(c, 0, 2)).toEqual({ tl: 'goals', tr: 'day', bl: 'body', br: 'habits' });
    expect(reorderDock(c, 3, 0)).toEqual({ tl: 'habits', tr: 'body', bl: 'goals', br: 'day' });
  });
  it('keeps an empty or Tasks slot in place and ignores a bad index', () => {
    const c = { tl: 'body', tr: null, bl: 'tasks', br: 'habits' } as const;
    expect(reorderDock(c, 0, 1)).toEqual({ tl: 'habits', tr: null, bl: 'tasks', br: 'body' });
    expect(reorderDock(c, 0, 5)).toBe(c);
    expect(reorderDock(c, 1, 1)).toBe(c);
  });
});
