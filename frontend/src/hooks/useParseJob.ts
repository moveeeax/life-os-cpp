import { useCallback, useEffect, useRef, useState } from 'react';

import {
  afterPoll,
  enqueueErrorText,
  pollErrorText,
  type JobLike,
  type ParseState,
} from '@/lib/parseJob';

export type { ParseState } from '@/lib/parseJob';

export interface ParseJobOptions<TJob extends JobLike, TArgs extends unknown[]> {
  /** Queues the job; resolves to its first state (the id, status queued). */
  enqueue: (...args: TArgs) => Promise<TJob>;
  /** Reads the job once. */
  read: (id: string, signal: AbortSignal) => Promise<TJob>;
  /** Pause between two reads. */
  pollMs: number;
  /** Give up after this long ("parse_timeout"). */
  deadlineMs: number;
  /** The error text when the server has no reader configured. */
  notConfigured: string;
}

/**
 * One parse job of an add form: `start()` queues it, then the hook reads it
 * every `pollMs` until done, failed or the deadline. `cancel()` and unmount
 * drop the job, so a reopened form starts clean; an answer of an older job
 * is ignored.
 */
export function useParseJob<TJob extends JobLike, TArgs extends unknown[]>(
  options: ParseJobOptions<TJob, TArgs>,
) {
  const [state, setState] = useState<ParseState>('idle');
  const [job, setJob] = useState<TJob | null>(null);
  const [error, setError] = useState<string | null>(null);
  const opts = useRef(options);
  opts.current = options;
  const starting = useRef(false);
  // Bumped by every start and cancel: an answer of an older job is dropped.
  const generation = useRef(0);

  const start = useCallback(async (...args: TArgs) => {
    if (starting.current) return;
    starting.current = true;
    const mine = ++generation.current;
    setState('starting');
    setError(null);
    setJob(null);
    try {
      const first = await opts.current.enqueue(...args);
      if (generation.current !== mine) return;
      setJob(first);
      setState('running');
    } catch (e) {
      if (generation.current !== mine) return;
      setError(enqueueErrorText(e, opts.current.notConfigured));
      setState('failed');
    } finally {
      starting.current = false;
    }
  }, []);

  const cancel = useCallback(() => {
    generation.current++;
    starting.current = false;
    setState('idle');
    setJob(null);
    setError(null);
  }, []);

  const jobId = state === 'running' ? job?.id : undefined;

  useEffect(() => {
    if (!jobId) return;
    const mine = generation.current;
    const deadline = Date.now() + opts.current.deadlineMs;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const controller = new AbortController();
    const tick = async () => {
      if (generation.current !== mine) return;
      if (Date.now() > deadline) {
        setError('parse_timeout');
        setState('failed');
        return;
      }
      try {
        const data = await opts.current.read(jobId, controller.signal);
        if (generation.current !== mine) return;
        setJob(data);
        const end = afterPoll(data);
        if (end) {
          if (end.state === 'failed') setError(end.error);
          setState(end.state);
          return;
        }
      } catch (e) {
        if (generation.current !== mine || controller.signal.aborted) return;
        const gone = pollErrorText(e);
        if (gone) {
          setError(gone);
          setState('failed');
          return;
        }
        // A dropped connection: keep asking.
      }
      timer = setTimeout(() => void tick(), opts.current.pollMs);
    };
    void tick();
    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [jobId]);

  // Unmount drops the job; a remount (StrictMode does one in development) may start again.
  useEffect(
    () => () => {
      generation.current++;
      starting.current = false;
    },
    [],
  );

  return { state, job, error, start, cancel };
}
