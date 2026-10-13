import { describe, expect, it } from 'vitest';

import {
  FOCUS_LENGTHS,
  fadeVolume,
  DONE_KEEP_MS,
  focusKey,
  pauseSession,
  resumeSession,
  sessionFromStorage,
  sessionState,
  startSession,
} from './focus';

const now = 1_000_000_000;

describe('focus sessions', () => {
  it('offers 25, 50 and 90 minutes', () => {
    expect(FOCUS_LENGTHS).toEqual([25, 50, 90]);
  });

  it('is idle without a session', () => {
    expect(sessionState(null, now)).toEqual({ phase: 'idle', left: 0, minutes: 0 });
  });

  it('runs from the start to the end time', () => {
    const s = startSession(25, now);
    expect(sessionState(s, now)).toEqual({ phase: 'running', left: 1500, minutes: 25 });
    expect(sessionState(s, now + 60_500)).toEqual({ phase: 'running', left: 1440, minutes: 25 });
  });

  it('is done at the end, never negative', () => {
    const s = startSession(25, now);
    expect(sessionState(s, now + 25 * 60_000)).toEqual({ phase: 'done', left: 0, minutes: 25 });
    expect(sessionState(s, now + 99 * 60_000).left).toBe(0);
  });

  it('keeps the time left while paused, however long the pause', () => {
    const paused = pauseSession(startSession(50, now), now + 10 * 60_000);
    expect(sessionState(paused, now + 10 * 60_000)).toEqual({
      phase: 'paused',
      left: 2400,
      minutes: 50,
    });
    expect(sessionState(paused, now + 20 * 3_600_000)).toEqual({
      phase: 'paused',
      left: 2400,
      minutes: 50,
    });
    const resumed = resumeSession(paused, now + 20 * 3_600_000);
    expect(sessionState(resumed, now + 20 * 3_600_000)).toEqual({
      phase: 'running',
      left: 2400,
      minutes: 50,
    });
  });

  it('keeps one session per user', () => {
    expect(focusKey('u1')).toBe('now:focus:u1');
  });
});

describe('fadeVolume', () => {
  it('rises to the target over the fade and falls back to zero', () => {
    expect(fadeVolume(0.6, 0, 3000, 'in')).toBe(0);
    expect(fadeVolume(0.6, 1500, 3000, 'in')).toBeCloseTo(0.3);
    expect(fadeVolume(0.6, 4000, 3000, 'in')).toBe(0.6);
    expect(fadeVolume(0.6, 0, 3000, 'out')).toBe(0.6);
    expect(fadeVolume(0.6, 3000, 3000, 'out')).toBe(0);
  });
});

describe('a finished session', () => {
  it('stays done for a while, then goes back to idle', () => {
    const s = startSession(25, now, '2026-10-13');
    const end = now + 25 * 60_000;
    expect(sessionState(s, end + 1000).phase).toBe('done');
    expect(sessionState(s, end + DONE_KEEP_MS + 1000)).toEqual({
      phase: 'idle',
      left: 0,
      minutes: 25,
    });
  });

  it('remembers the day of its focus', () => {
    expect(startSession(25, now, '2026-10-13').focusDate).toBe('2026-10-13');
    expect(pauseSession(startSession(25, now, '2026-10-13'), now).focusDate).toBe('2026-10-13');
  });
});

describe('sessionFromStorage', () => {
  const key = focusKey('u1');
  it('takes a session started in another tab', () => {
    const s = startSession(25, now);
    expect(sessionFromStorage({ key, newValue: JSON.stringify(s) }, key)).toEqual(s);
  });
  it('reads an exit or a cleared storage as no session', () => {
    expect(sessionFromStorage({ key, newValue: 'null' }, key)).toBeNull();
    expect(sessionFromStorage({ key, newValue: null }, key)).toBeNull();
    expect(sessionFromStorage({ key: null, newValue: null }, key)).toBeNull();
  });
  it('ignores other keys and broken values', () => {
    expect(sessionFromStorage({ key: 'other', newValue: '{}' }, key)).toBeUndefined();
    expect(sessionFromStorage({ key, newValue: '{oops' }, key)).toBeUndefined();
  });
});
