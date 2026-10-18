// @vitest-environment jsdom
import { QueryClient, QueryClientProvider } from '@tanstack/react-query';
import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import type { ReactNode } from 'react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { ApiClientError } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import { QUEUE_KEY, type QueuedSet } from '@/lib/workout/session';
import type { WorkoutSession, WorkoutSet, WorkoutSetInput } from '@/lib/workout/types';

const { putJson, deleteJson } = vi.hoisted(() => ({ putJson: vi.fn(), deleteJson: vi.fn() }));
vi.mock('@/lib/api/client', async () => {
  const actual = await vi.importActual<typeof import('@/lib/api/client')>('@/lib/api/client');
  return { ...actual, api: { ...actual.api, putJson, deleteJson } };
});

import { useSetQueue } from './useWorkoutSession';

/**
 * The unsent-set queue rendered for real against a QueryClient: sets go
 * out one at a time in order, a refused set (4xx) is marked rejected and
 * never resent, a lost connection is retried with backoff, and a saved
 * set lands in the cached session only after in-flight refetches of it
 * were cancelled.
 */

const SESSION = 's1';

function body(position: number): WorkoutSetInput {
  return { session_exercise_id: 'ex1', position, kind: 'work', reps: 5, weight_kg: 100 };
}

function savedSet(id: string, position: number): WorkoutSet {
  return {
    id,
    session_exercise_id: 'ex1',
    position,
    kind: 'work',
    weight_kg: 100,
    reps: 5,
    duration_seconds: null,
    distance_m: null,
    rpe: null,
    completed_at: '2026-10-14T10:00:00Z',
  } as WorkoutSet;
}

function session(): WorkoutSession {
  return {
    id: SESSION,
    finished_at: null,
    exercises: [{ id: 'ex1', sets: [] }],
  } as unknown as WorkoutSession;
}

/** A promise the test settles by hand. */
function deferred<T>() {
  let resolve!: (v: T) => void;
  let reject!: (e: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

let qc: QueryClient;

function wrapper({ children }: { children: ReactNode }) {
  return <QueryClientProvider client={qc}>{children}</QueryClientProvider>;
}

function stored(): QueuedSet[] {
  return JSON.parse(localStorage.getItem(QUEUE_KEY) ?? '[]') as QueuedSet[];
}

beforeEach(() => {
  qc = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  qc.setQueryData(qk.workout.session(SESSION), session());
  qc.setQueryData(qk.workout.active(), session());
  localStorage.clear();
  putJson.mockReset();
  deleteJson.mockReset();
});

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

describe('useSetQueue', () => {
  it('sends queued sets one after another, in order', async () => {
    const first = deferred<{ data: WorkoutSet }>();
    putJson.mockImplementationOnce(() => first.promise);
    putJson.mockImplementationOnce(async (path: string) => ({
      data: savedSet(path.slice(path.lastIndexOf('/') + 1), 2),
    }));
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });

    act(() => {
      result.current.put('a', body(1));
      result.current.put('b', body(2));
    });
    expect(result.current.queue.map((q) => q.id)).toEqual(['a', 'b']);
    // Only the first is out while it is in flight.
    expect(putJson).toHaveBeenCalledTimes(1);
    expect(putJson.mock.calls[0][0]).toBe('/api/v1/workout/sets/a');

    await act(async () => {
      first.resolve({ data: savedSet('a', 1) });
      await first.promise;
    });
    await waitFor(() => expect(putJson).toHaveBeenCalledTimes(2));
    expect(putJson.mock.calls[1][0]).toBe('/api/v1/workout/sets/b');
    await waitFor(() => expect(result.current.queue).toEqual([]));
    expect(stored()).toEqual([]);

    const cached = qc.getQueryData<WorkoutSession>(qk.workout.session(SESSION));
    expect(cached?.exercises[0].sets.map((s) => s.id)).toEqual(['a', 'b']);
  });

  it('a 4xx marks the set rejected and does not resend it', async () => {
    putJson.mockRejectedValueOnce(
      new ApiClientError({ status: 422, message: 'reps must be positive' }),
    );
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    act(() => result.current.put('a', body(1)));
    await waitFor(() => expect(result.current.queue[0]?.rejected).toBe('reps must be positive'));
    expect(result.current.queue[0].attempts).toBe(1);
    await new Promise((r) => setTimeout(r, 20));
    expect(putJson).toHaveBeenCalledTimes(1);
    // The rejection survives a reload.
    expect(stored()[0].rejected).toBe('reps must be positive');
  });

  it('a lost connection is retried with backoff', async () => {
    vi.useFakeTimers();
    putJson.mockRejectedValueOnce(new ApiClientError({ status: 0, message: 'offline' }));
    putJson.mockRejectedValueOnce(new ApiClientError({ status: 503, message: 'busy' }));
    putJson.mockResolvedValueOnce({ data: savedSet('a', 1) });
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    await act(async () => {
      result.current.put('a', body(1));
      await vi.advanceTimersByTimeAsync(0);
    });
    expect(putJson).toHaveBeenCalledTimes(1);
    expect(result.current.queue[0]).toMatchObject({ id: 'a', attempts: 1, rejected: undefined });

    // backoffMs(1) = 1 s: nothing before it, the second attempt at it.
    await act(() => vi.advanceTimersByTimeAsync(999));
    expect(putJson).toHaveBeenCalledTimes(1);
    await act(() => vi.advanceTimersByTimeAsync(1));
    expect(putJson).toHaveBeenCalledTimes(2);
    expect(result.current.queue[0].attempts).toBe(2);

    // backoffMs(2) = 2 s, then the set goes through.
    await act(() => vi.advanceTimersByTimeAsync(2000));
    expect(putJson).toHaveBeenCalledTimes(3);
    expect(result.current.queue).toEqual([]);
  });

  it('cancels refetches of the session before patching the cache', async () => {
    putJson.mockResolvedValueOnce({ data: savedSet('a', 1) });
    const order: string[] = [];
    vi.spyOn(qc, 'cancelQueries').mockImplementation(async (filters) => {
      order.push(`cancel:${JSON.stringify(filters?.queryKey)}`);
    });
    const setQueryData = qc.setQueryData.bind(qc);
    vi.spyOn(qc, 'setQueryData').mockImplementation((key, updater) => {
      order.push(`set:${JSON.stringify(key)}`);
      return setQueryData(key, updater);
    });
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    act(() => result.current.put('a', body(1)));
    await waitFor(() => expect(result.current.queue).toEqual([]));

    const cancels = order.filter((o) => o.startsWith('cancel:'));
    const sets = order.filter((o) => o.startsWith('set:'));
    expect(cancels).toEqual([
      `cancel:${JSON.stringify(qk.workout.active())}`,
      `cancel:${JSON.stringify(qk.workout.session(SESSION))}`,
    ]);
    expect(sets.length).toBeGreaterThan(0);
    expect(order.indexOf(cancels[1])).toBeLessThan(order.indexOf(sets[0]));
  });

  it('an edit made while the old version was in flight stays queued', async () => {
    const first = deferred<{ data: WorkoutSet }>();
    putJson.mockImplementationOnce(() => first.promise);
    putJson.mockResolvedValueOnce({ data: savedSet('a', 1) });
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    act(() => result.current.put('a', body(1)));
    act(() => result.current.put('a', { ...body(1), reps: 6 }));
    await act(async () => {
      first.resolve({ data: savedSet('a', 1) });
      await first.promise;
    });
    await waitFor(() => expect(putJson).toHaveBeenCalledTimes(2));
    expect(putJson.mock.calls[1][1]).toEqual({ body: { ...body(1), reps: 6 } });
  });

  it('a set deleted while its PUT is in flight is deleted once the PUT lands', async () => {
    const first = deferred<{ data: WorkoutSet }>();
    putJson.mockImplementationOnce(() => first.promise);
    deleteJson.mockResolvedValueOnce(undefined);
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    act(() => result.current.put('a', body(1)));
    await act(() => result.current.remove('a', false));
    expect(result.current.queue).toEqual([]);
    // No DELETE before the PUT settles: it could reach the server first.
    expect(deleteJson).not.toHaveBeenCalled();

    await act(async () => {
      first.resolve({ data: savedSet('a', 1) });
      await first.promise;
    });
    await waitFor(() => expect(deleteJson).toHaveBeenCalledWith('/api/v1/workout/sets/a'));
    const cached = qc.getQueryData<WorkoutSession>(qk.workout.session(SESSION));
    expect(cached?.exercises[0].sets).toEqual([]);
  });

  it('a set deleted while its PUT is out stays deleted when the PUT is lost', async () => {
    const first = deferred<{ data: WorkoutSet }>();
    putJson.mockImplementationOnce(() => first.promise);
    deleteJson.mockRejectedValueOnce(new ApiClientError({ status: 404, message: 'gone' }));
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    act(() => result.current.put('a', body(1)));
    await act(() => result.current.remove('a', false));
    await act(async () => {
      first.reject(new ApiClientError({ status: 0, message: 'offline' }));
      await first.promise.catch(() => undefined);
    });
    // The answer was lost, so the set may be stored: a DELETE goes out, a 404 is fine.
    await waitFor(() => expect(deleteJson).toHaveBeenCalledTimes(1));
    await new Promise((r) => setTimeout(r, 20));
    expect(putJson).toHaveBeenCalledTimes(1);
    expect(result.current.queue).toEqual([]);
  });

  it('drops rejected sets of other sessions on mount, keeps unsent ones', () => {
    localStorage.setItem(
      QUEUE_KEY,
      JSON.stringify([
        { id: 'x', sessionId: 'other', body: body(1), attempts: 3, rejected: 'no such session' },
        { id: 'y', sessionId: 'other', body: body(1), attempts: 1 },
      ] satisfies QueuedSet[]),
    );
    putJson.mockImplementation(() => new Promise(() => {}));
    const { result } = renderHook(() => useSetQueue(SESSION), { wrapper });
    expect(result.current.queue.map((q) => q.id)).toEqual(['y']);
  });
});
