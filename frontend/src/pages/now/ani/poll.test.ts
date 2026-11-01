// @vitest-environment jsdom
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { POLL_FAST_FOR_MS, POLL_MS, POLL_SLOW_MS, pollDelay, pollTurn } from './chat';

function setHidden(hidden: boolean) {
  Object.defineProperty(document, 'visibilityState', {
    configurable: true,
    get: () => (hidden ? 'hidden' : 'visible'),
  });
  document.dispatchEvent(new Event('visibilitychange'));
}

describe('pollDelay', () => {
  it('reads every second at first, then every five seconds', () => {
    expect(pollDelay(0)).toBe(POLL_MS);
    expect(pollDelay(POLL_FAST_FOR_MS - 1)).toBe(POLL_MS);
    expect(pollDelay(POLL_FAST_FOR_MS)).toBe(POLL_SLOW_MS);
    expect(pollDelay(10 * 60_000)).toBe(POLL_SLOW_MS);
  });
});

describe('pollTurn', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    setHidden(false);
  });
  afterEach(() => {
    setHidden(false);
    vi.useRealTimers();
  });

  it('backs off after ten seconds: about 10 reads in the first 10 s, then one per 5 s', async () => {
    const read = vi.fn(() => Promise.resolve());
    const stop = pollTurn(read);
    await vi.advanceTimersByTimeAsync(POLL_FAST_FOR_MS);
    const fast = read.mock.calls.length;
    expect(fast).toBeGreaterThanOrEqual(9);
    expect(fast).toBeLessThanOrEqual(10);
    await vi.advanceTimersByTimeAsync(60_000);
    // A minute at the slow pace is twelve reads, not sixty.
    expect(read.mock.calls.length - fast).toBeLessThanOrEqual(13);
    expect(read.mock.calls.length - fast).toBeGreaterThanOrEqual(11);
    stop();
  });

  it('sends one read at a time', async () => {
    let settle: () => void = () => undefined;
    const read = vi.fn(
      () =>
        new Promise<void>((r) => {
          settle = r;
        }),
    );
    const stop = pollTurn(read);
    await vi.advanceTimersByTimeAsync(POLL_MS * 5);
    expect(read).toHaveBeenCalledTimes(1);
    settle();
    await vi.advanceTimersByTimeAsync(POLL_MS);
    expect(read).toHaveBeenCalledTimes(2);
    stop();
  });

  it('reads nothing while the tab is hidden and reads at once when it shows again', async () => {
    const read = vi.fn(() => Promise.resolve());
    const stop = pollTurn(read);
    await vi.advanceTimersByTimeAsync(POLL_MS);
    expect(read).toHaveBeenCalledTimes(1);
    setHidden(true);
    await vi.advanceTimersByTimeAsync(2 * 60_000);
    expect(read).toHaveBeenCalledTimes(1);
    setHidden(false);
    await vi.advanceTimersByTimeAsync(0);
    expect(read).toHaveBeenCalledTimes(2);
    await vi.advanceTimersByTimeAsync(POLL_SLOW_MS);
    expect(read).toHaveBeenCalledTimes(3);
    stop();
  });

  it('a failed read keeps polling, and stop ends it', async () => {
    const read = vi.fn(() => Promise.reject(new Error('net')));
    const stop = pollTurn(read);
    await vi.advanceTimersByTimeAsync(POLL_MS * 3);
    expect(read).toHaveBeenCalledTimes(3);
    stop();
    await vi.advanceTimersByTimeAsync(60_000);
    expect(read).toHaveBeenCalledTimes(3);
  });
});
