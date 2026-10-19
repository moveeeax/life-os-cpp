// @vitest-environment jsdom
import { act, cleanup, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { REST_KEY } from '@/lib/workout/session';

import { startRest, stopRest, useRestTimer } from './useWorkoutSession';

/**
 * The rest countdown is started from the page through startRest and read by
 * the bar alone (useRestTimer), so the page does not re-render on every tick.
 */
beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date('2026-10-18T10:00:00Z'));
  localStorage.clear();
});

afterEach(() => {
  act(() => stopRest());
  cleanup();
  vi.useRealTimers();
});

describe('rest timer', () => {
  it('a start outside the bar shows up in it and counts down', () => {
    const { result } = renderHook(() => useRestTimer());
    expect(result.current.running).toBe(false);

    act(() => startRest(90));
    expect(result.current.running).toBe(true);
    expect(result.current.remaining).toBe(90);
    expect(localStorage.getItem(REST_KEY)).toBe(String(Date.now() + 90_000));

    act(() => {
      vi.advanceTimersByTime(30_000);
    });
    expect(result.current.remaining).toBe(60);
  });

  it('ends on its own, flags justEnded and clears the stored end', () => {
    const { result } = renderHook(() => useRestTimer());
    act(() => startRest(2));
    act(() => {
      vi.advanceTimersByTime(2_500);
    });
    expect(result.current.running).toBe(false);
    expect(result.current.justEnded).toBe(true);
    expect(localStorage.getItem(REST_KEY)).toBeNull();
  });

  it('stopRest stops it', () => {
    const { result } = renderHook(() => useRestTimer());
    act(() => startRest(60));
    act(() => stopRest());
    expect(result.current.running).toBe(false);
    expect(result.current.justEnded).toBe(false);
  });
});
