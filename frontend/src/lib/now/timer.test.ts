import { describe, expect, it } from 'vitest';

import { FOCUS_MINUTES, timerKey, timerState } from './timer';

describe('timerState', () => {
  const now = 1_000_000_000;
  it('is idle without an end time', () => {
    expect(timerState(null, now)).toEqual({ running: false, left: 0, done: false });
  });
  it('counts the seconds left while running', () => {
    expect(timerState(now + 90_500, now)).toEqual({ running: true, left: 91, done: false });
  });
  it('is done, never negative, once the end has passed', () => {
    expect(timerState(now - 5_000, now)).toEqual({ running: false, left: 0, done: true });
    expect(timerState(now, now)).toEqual({ running: false, left: 0, done: true });
  });
  it('keeps one key per user', () => {
    expect(timerKey('u1')).toBe('now:timer:u1');
    expect(FOCUS_MINUTES).toBe(25);
  });
});
