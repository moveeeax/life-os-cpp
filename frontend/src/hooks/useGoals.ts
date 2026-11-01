import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { NOW_DIGEST, qk } from '@/lib/api/queryKeys';
import { localZone } from '@/lib/tasks';
import type {
  Goal,
  GoalCheckin,
  GoalDetail,
  GoalInput,
  GoalMilestone,
  GoalPatch,
  GoalSection,
} from '@/lib/goals';
import { act } from '@/lib/now/act';

// Paths with an id go through the client's untyped overload (it does not fill
// path templates); the response types come from the same OpenAPI schemas.
const BASE = '/api/v1/goals';

/** A goal write moves its tasks too (and a task write moves its goal), and Now's digest. */
export const GOAL_WRITES = [qk.goals.all(), qk.tasks.all(), ...NOW_DIGEST] as const;
const ALL = GOAL_WRITES;

// The goals routes count a goal's default day and the days of its source in
// the caller's zone: every read and write sends the browser's IANA zone.

export async function fetchGoals(
  status: 'active' | 'done' | 'dropped',
  date: string,
  tz: string,
  signal?: AbortSignal,
): Promise<Goal[]> {
  return (await api.getJson<{ data: Goal[] }>(BASE, { query: { status, date, tz }, signal })).data;
}

export async function fetchGoal(
  id: string,
  date: string,
  tz: string,
  signal?: AbortSignal,
): Promise<GoalDetail> {
  return (await api.getJson<{ data: GoalDetail }>(`${BASE}/${id}`, { query: { date, tz }, signal }))
    .data;
}

/** A new goal (POST), or the changed fields of one (PATCH). */
export async function saveGoal(
  id: string | null,
  body: GoalInput | GoalPatch,
  tz: string,
): Promise<Goal> {
  const query = { tz };
  return id
    ? (await api.patchJson<{ data: Goal }>(`${BASE}/${id}`, { query, body })).data
    : (await api.postJson<{ data: Goal }>(BASE, { query, body })).data;
}

export function useGoals(status: 'active' | 'done' | 'dropped', date: string) {
  const tz = localZone();
  return useQuery({
    queryKey: qk.goals.list(status, date, tz),
    queryFn: ({ signal }) => fetchGoals(status, date, tz, signal),
  });
}

export function useGoal(id: string | null, date: string) {
  const tz = localZone();
  return useQuery({
    queryKey: qk.goals.detail(id ?? '', date, tz),
    enabled: id !== null,
    queryFn: ({ signal }) => fetchGoal(id ?? '', date, tz, signal),
  });
}

export function useSaveGoal(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: GoalInput | GoalPatch) => {
      const goal = await saveGoal(id, body, localZone());
      if ('status' in body && body.status === 'done')
        act({ kind: 'goal_done', title: (goal as { title?: string })?.title ?? body.title ?? '' });
      return goal;
    },
    {
      invalidate: ALL,
      onSuccess,
    },
  );
}

export function useDeleteGoal(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${id}`), {
    invalidate: ALL,
    onSuccess,
  });
}

export function usePutCheckin(goalId: string, onSuccess?: () => void) {
  return useApiMutation(
    async (body: { value: number; date: string; note?: string }) =>
      (await api.postJson<{ data: GoalCheckin }>(`${BASE}/${goalId}/checkins`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}

export function useAddSection(goalId: string, onSuccess?: () => void) {
  return useApiMutation(
    async (name: string) =>
      (await api.postJson<{ data: GoalSection }>(`${BASE}/${goalId}/sections`, { body: { name } }))
        .data,
    { invalidate: ALL, onSuccess },
  );
}

export function useAddMilestone(goalId: string, onSuccess?: () => void) {
  return useApiMutation(
    async (body: { date: string; label: string }) =>
      (await api.postJson<{ data: GoalMilestone }>(`${BASE}/${goalId}/milestones`, { body })).data,
    { invalidate: ALL, onSuccess },
  );
}
