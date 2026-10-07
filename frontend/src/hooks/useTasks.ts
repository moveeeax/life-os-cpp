import { useCallback, useEffect, useRef, useState } from 'react';
import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { ApiClientError, api, apiErrorMessage } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import {
  localToday,
  type Task,
  type TaskAgenda,
  type TaskInput,
  type TaskNote,
  type TaskParseJob,
  type TaskParseLine,
  type TaskPatch,
} from '@/lib/tasks';

// Paths with an id go through the client's untyped overload (it does not fill
// path templates); the response types come from the same OpenAPI schemas.
const BASE = '/api/v1/tasks';

/** Every write invalidates the whole section: the agenda, the lists and the inbox move together. */
const ALL = [qk.tasks.all()] as const;

export const TASKS_PARSE_POLL_MS = 1500;
/** The provider call is capped by its timeout; a job unfinished after this is stuck. */
export const TASKS_PARSE_DEADLINE_MS = 3 * 60 * 1000;

// ── reads ──────────────────────────────────────────────────────────────────

/** Whether the tasks module answers at all (404 while switched off). */
export function useTasksEnabled(signedIn: boolean): boolean {
  const probe = useQuery({
    queryKey: qk.tasks.enabled(),
    enabled: signedIn,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) => {
      const { error } = await api.GET(`${BASE}/status` as string, { signal });
      return !(error && error.status === 404);
    },
  });
  return probe.data ?? true;
}

export function useTasksStatus() {
  return useQuery({
    queryKey: qk.tasks.status(),
    staleTime: 5 * 60_000,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: { llm_available: boolean } }>(`${BASE}/status`, { signal })).data,
  });
}

/** The agenda of the person's local day, read in their zone. */
export function useAgenda(date: string, tz: string) {
  return useQuery({
    queryKey: qk.tasks.agenda(date, tz),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: TaskAgenda }>(`${BASE}/agenda`, { query: { date, tz }, signal }))
        .data,
  });
}

export function useNotes(status: 'inbox' | 'archived' = 'inbox') {
  return useQuery({
    queryKey: qk.tasks.notes(status),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: TaskNote[] }>(`${BASE}/notes`, { query: { status }, signal }))
        .data,
  });
}

// ── writes ─────────────────────────────────────────────────────────────────

export function useCreateTask(onSuccess?: (task: Task) => void) {
  return useApiMutation(
    async (body: TaskInput) => (await api.postJson<{ data: Task }>(`${BASE}/items`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useUpdateTask(id: string, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TaskPatch) =>
      (await api.patchJson<{ data: Task }>(`${BASE}/items/${id}`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useDeleteTask(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/items/${id}`), {
    invalidate: ALL,
    onSuccess,
  });
}

export function useCreateNote(onSuccess?: () => void) {
  return useApiMutation(
    async (text: string) =>
      (await api.postJson<{ data: TaskNote }>(`${BASE}/notes`, { body: { text } })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useUpdateNote(onSuccess?: () => void) {
  return useApiMutation(
    async ({ id, ...body }: { id: string; text?: string; status?: 'inbox' | 'archived' }) =>
      (await api.patchJson<{ data: TaskNote }>(`${BASE}/notes/${id}`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

// ── the parse ──────────────────────────────────────────────────────────────

export type TasksParseState = 'idle' | 'starting' | 'running' | 'done' | 'failed';

/**
 * One phrase through the tasks_parse job: start, poll until done or failed
 * (or the deadline). cancel() and unmount drop the job.
 */
export function useTasksParse() {
  const [state, setState] = useState<TasksParseState>('idle');
  const [job, setJob] = useState<TaskParseJob | null>(null);
  const [error, setError] = useState<string | null>(null);
  const starting = useRef(false);
  const generation = useRef(0);

  const start = useCallback(async (text: string, noteId?: string) => {
    if (starting.current) return;
    starting.current = true;
    const mine = ++generation.current;
    setState('starting');
    setError(null);
    setJob(null);
    try {
      const { data } = await api.postJson<{ data: { id: string } }>(`${BASE}/parse`, {
        body: { text, hint_date: localToday(), ...(noteId ? { note_id: noteId } : {}) },
      });
      if (generation.current !== mine) return;
      setJob({ id: data.id, status: 'queued' } as TaskParseJob);
      setState('running');
    } catch (e) {
      if (generation.current !== mine) return;
      setError(
        e instanceof ApiClientError && e.code === 'not_configured'
          ? 'not_configured'
          : apiErrorMessage(e),
      );
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
    const deadline = Date.now() + TASKS_PARSE_DEADLINE_MS;
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
        const { data } = await api.getJson<{ data: TaskParseJob }>(`${BASE}/parse/${jobId}`, {
          signal: controller.signal,
        });
        if (generation.current !== mine) return;
        setJob(data);
        if (data.status === 'done') {
          setState('done');
          return;
        }
        if (data.status === 'failed') {
          setError(data.error);
          setState('failed');
          return;
        }
      } catch (e) {
        if (generation.current !== mine || controller.signal.aborted) return;
        if (e instanceof ApiClientError && e.status === 404) {
          setError('The parse job is gone.');
          setState('failed');
          return;
        }
      }
      timer = setTimeout(() => void tick(), TASKS_PARSE_POLL_MS);
    };
    void tick();
    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [jobId]);

  useEffect(() => () => void generation.current++, []);

  return { state, job, error, start, cancel };
}

export function useAcceptTasksParse(jobId: string | undefined, onSuccess?: () => void) {
  return useApiMutation(
    async (lines: TaskParseLine[]) =>
      (
        await api.postJson<{ data: Task[] }>(`${BASE}/parse/${jobId}/accept`, {
          body: {
            lines: lines.map(({ title, area, effort, due, next_step }) => ({
              title,
              area,
              effort,
              due,
              next_step,
            })),
          },
        })
      ).data,
    { invalidate: ALL, onSuccess },
  );
}
