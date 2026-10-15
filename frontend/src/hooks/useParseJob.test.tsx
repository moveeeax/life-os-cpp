// @vitest-environment jsdom
import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { ApiClientError } from '@/lib/api/client';

import { useParseJob, type ParseJobOptions } from './useParseJob';

/**
 * The parse-job hook rendered for real: the generation counter must drop
 * the answer of a job that was cancelled, replaced or unmounted, and the
 * poll of a replaced job must be aborted.
 */

interface Job {
  id: string;
  status: string;
  error?: string | null;
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

function options(
  over: Partial<ParseJobOptions<Job, [string]>> = {},
): ParseJobOptions<Job, [string]> {
  return {
    enqueue: async (id: string) => ({ id, status: 'queued' }),
    read: async (id: string) => ({ id, status: 'done' }),
    pollMs: 5,
    deadlineMs: 60_000,
    notConfigured: 'No reader is configured.',
    ...over,
  };
}

afterEach(() => {
  cleanup();
});

describe('useParseJob', () => {
  it('queues, polls until done and exposes the job', async () => {
    const reads: string[] = [];
    let polls = 0;
    const { result } = renderHook(() =>
      useParseJob(
        options({
          read: async (id) => {
            reads.push(id);
            polls += 1;
            return { id, status: polls < 3 ? 'running' : 'done' };
          },
        }),
      ),
    );
    expect(result.current.state).toBe('idle');
    await act(() => result.current.start('job-1'));
    await waitFor(() => expect(result.current.state).toBe('done'));
    expect(reads).toEqual(['job-1', 'job-1', 'job-1']);
    expect(result.current.job).toEqual({ id: 'job-1', status: 'done' });
  });

  it('a failed job carries its error; a 404 on the poll ends it', async () => {
    const { result } = renderHook(() =>
      useParseJob(
        options({ read: async (id) => ({ id, status: 'failed', error: 'bad receipt' }) }),
      ),
    );
    await act(() => result.current.start('j'));
    await waitFor(() => expect(result.current.state).toBe('failed'));
    expect(result.current.error).toBe('bad receipt');

    const gone = renderHook(() =>
      useParseJob(
        options({
          read: async () => {
            throw new ApiClientError({ status: 404, message: 'not found' });
          },
        }),
      ),
    );
    await act(() => gone.result.current.start('j'));
    await waitFor(() => expect(gone.result.current.state).toBe('failed'));
    expect(gone.result.current.error).toBe('The parse job is gone.');
  });

  it('cancel drops the answer of a start still in flight', async () => {
    const first = deferred<Job>();
    const { result } = renderHook(() => useParseJob(options({ enqueue: () => first.promise })));
    act(() => void result.current.start('a'));
    expect(result.current.state).toBe('starting');

    act(() => result.current.cancel());
    expect(result.current.state).toBe('idle');

    // The old enqueue answers after the cancel: nothing may change.
    await act(async () => {
      first.resolve({ id: 'a', status: 'queued' });
      await first.promise;
    });
    expect(result.current.state).toBe('idle');
    expect(result.current.job).toBeNull();
  });

  it('cancel while running stops the polling and ignores the pending read', async () => {
    const read = deferred<Job>();
    let reads = 0;
    const { result } = renderHook(() =>
      useParseJob(
        options({
          read: () => {
            reads += 1;
            return read.promise;
          },
        }),
      ),
    );
    await act(() => result.current.start('a'));
    expect(result.current.state).toBe('running');
    await waitFor(() => expect(reads).toBe(1));

    act(() => result.current.cancel());
    await act(async () => {
      read.resolve({ id: 'a', status: 'done' });
      await read.promise;
    });
    await new Promise((r) => setTimeout(r, 30));
    expect(result.current.state).toBe('idle');
    expect(reads).toBe(1);
  });

  it('a new start aborts the poll of the old job and takes over', async () => {
    const signals: AbortSignal[] = [];
    const reads: string[] = [];
    const { result } = renderHook(() =>
      useParseJob(
        options({
          read: (id, signal) => {
            signals.push(signal);
            reads.push(id);
            return new Promise<Job>((resolve, reject) => {
              signal.addEventListener('abort', () =>
                reject(new DOMException('aborted', 'AbortError')),
              );
              if (id === 'second') resolve({ id, status: 'done' });
            });
          },
        }),
      ),
    );
    await act(() => result.current.start('first'));
    await waitFor(() => expect(reads).toEqual(['first']));

    // start() is a no-op while a start is in flight, and a cancel clears that; a
    // second job after a cancel is the "job change" of an add form.
    act(() => result.current.cancel());
    await act(() => result.current.start('second'));
    await waitFor(() => expect(result.current.state).toBe('done'));
    expect(signals[0].aborted).toBe(true);
    expect(result.current.job?.id).toBe('second');
  });

  it('unmount aborts the poll and drops the answer', async () => {
    const read = deferred<Job>();
    let signal: AbortSignal | undefined;
    const setState = vi.fn();
    const { result, unmount } = renderHook(() => {
      const hook = useParseJob(
        options({
          read: (_id, s) => {
            signal = s;
            return read.promise;
          },
        }),
      );
      setState(hook.state);
      return hook;
    });
    await act(() => result.current.start('a'));
    await waitFor(() => expect(signal).toBeDefined());
    unmount();
    expect(signal!.aborted).toBe(true);
    const renders = setState.mock.calls.length;
    await act(async () => {
      read.resolve({ id: 'a', status: 'done' });
      await read.promise;
    });
    // No render after unmount: the resolved read was ignored.
    expect(setState.mock.calls.length).toBe(renders);
  });

  it('gives up at the deadline', async () => {
    const { result } = renderHook(() =>
      useParseJob(options({ deadlineMs: 0, read: async (id) => ({ id, status: 'running' }) })),
    );
    await act(() => result.current.start('slow'));
    await waitFor(() => expect(result.current.state).toBe('failed'));
    expect(result.current.error).toBe('parse_timeout');
  });
});
