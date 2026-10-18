import { describe, expect, it } from 'vitest';

import { DRAG_SLOP, dropIndex } from './useDockDrag';

const rect = (left: number, top: number, w: number, h: number) =>
  ({ left, top, width: w, height: h, right: left + w, bottom: top + h }) as DOMRect;

describe('dropIndex', () => {
  const row = [rect(0, 0, 100, 40), rect(100, 0, 100, 40), rect(200, 0, 100, 40)];
  it('picks the tile whose centre is nearest along the axis', () => {
    expect(dropIndex(row, 'x', 10)).toBe(0);
    expect(dropIndex(row, 'x', 149)).toBe(1);
    expect(dropIndex(row, 'x', 151)).toBe(1);
    expect(dropIndex(row, 'x', 240)).toBe(2);
    expect(dropIndex(row, 'x', 900)).toBe(2);
  });
  it('reads the vertical axis for the phone list', () => {
    const list = [rect(0, 0, 300, 50), rect(0, 50, 300, 50)];
    expect(dropIndex(list, 'y', 20)).toBe(0);
    expect(dropIndex(list, 'y', 80)).toBe(1);
  });
  it('keeps a small press a click', () => {
    expect(DRAG_SLOP).toBe(6);
  });
});
