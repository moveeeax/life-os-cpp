import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { useBatchMutation } from '@/hooks/useBatchMutation';
import { useModuleProbe } from '@/hooks/useModuleProbe';
import { useParseJob } from '@/hooks/useParseJob';
import { api } from '@/lib/api/client';
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

/**
 * Every write invalidates the whole section (the agenda, the lists and the
 * inbox move together), the goals, whose progress counts their tasks, and the
 * day's digest of Now, whose corners list them.
 */
// A task write also moves a trip's packing.
const ALL = [qk.tasks.all(), qk.goals.all(), qk.now.all(), qk.travel.all()] as const;

export const TASKS_PARSE_POLL_MS = 1500;
/** The provider call is capped by its timeout; a job unfinished after this is stuck. */
export const TASKS_PARSE_DEADLINE_MS = 3 * 60 * 1000;

// ── reads ──────────────────────────────────────────────────────────────────

/** [query key, url] of the probe that says whether the module is on. */
export const TASKS_PROBE = [qk.tasks.enabled(), `${BASE}/status`] as const;

/** Whether the tasks module answers at all (404 while switched off). */
export function useTasksEnabled(signedIn: boolean): boolean {
  return useModuleProbe(...TASKS_PROBE, signedIn) ?? true;
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

/** The inbox of Tasks; the API path stays /tasks/notes. */
export function useInbox(status: 'inbox' | 'archived' = 'inbox') {
  return useQuery({
    queryKey: qk.tasks.inbox(status),
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

/** Creates several tasks one after another (copying a packing list); one call, one error. */
export function useCreateTasks(onSuccess?: () => void) {
  return useBatchMutation(
    (body: TaskInput) => api.postJson<{ data: Task }>(`${BASE}/items`, { body }),
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

export type { ParseState as TasksParseState } from '@/hooks/useParseJob';

/**
 * One phrase through the tasks_parse job: start, poll until done or failed
 * (or the deadline). cancel() and unmount drop the job.
 */
export function useTasksParse() {
  return useParseJob({
    enqueue: async (text: string, noteId?: string) => {
      const { data } = await api.postJson<{ data: { id: string } }>(`${BASE}/parse`, {
        body: { text, hint_date: localToday(), ...(noteId ? { note_id: noteId } : {}) },
      });
      return { id: data.id, status: 'queued' } as TaskParseJob;
    },
    read: async (id, signal) =>
      (await api.getJson<{ data: TaskParseJob }>(`${BASE}/parse/${id}`, { signal })).data,
    pollMs: TASKS_PARSE_POLL_MS,
    deadlineMs: TASKS_PARSE_DEADLINE_MS,
    notConfigured: 'not_configured',
  });
}

export function useAcceptTasksParse(jobId: string | undefined, onSuccess?: () => void) {
  return useApiMutation(
    async (lines: (TaskParseLine & { project_id?: string | null })[]) =>
      (
        await api.postJson<{ data: Task[] }>(`${BASE}/parse/${jobId}/accept`, {
          body: {
            lines: lines.map(({ title, area, effort, due, next_step, project_id }) => ({
              title,
              area,
              effort,
              due,
              next_step,
              ...(project_id ? { project_id } : {}),
            })),
          },
        })
      ).data,
    { invalidate: ALL, onSuccess },
  );
}
