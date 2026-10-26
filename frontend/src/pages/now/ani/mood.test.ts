// @vitest-environment jsdom
import { beforeEach, describe, expect, it } from 'vitest';

import { moodLevel, moodOf, moveMood } from './mood';

describe('mood', () => {
  beforeEach(() => localStorage.clear());

  it('adds up through the day and starts afresh the next one', () => {
    expect(moodOf('2026-10-26')).toBe(0);
    moveMood('2026-10-26', 2);
    expect(moveMood('2026-10-26', 3)).toBe(5);
    expect(moodOf('2026-10-27')).toBe(0);
  });

  it('stays between -10 and 10', () => {
    for (let i = 0; i < 8; i++) moveMood('2026-10-26', -2);
    expect(moodOf('2026-10-26')).toBe(-10);
  });

  it('reads as cross, usual or happy', () => {
    expect([-3, -2, 0, 3, 4].map(moodLevel)).toEqual([-1, 0, 0, 0, 1]);
  });
});
