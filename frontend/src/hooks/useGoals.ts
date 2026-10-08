import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type {
  Goal,
  GoalCheckin,
  GoalDetail,
  GoalInput,
  GoalMilestone,
  GoalPatch,
  GoalSection,
} from '@/lib/goals';

// Paths with an id go through the client's untyped overload (it does not fill
// path templates); the response types come from the same OpenAPI schemas.
const BASE = '/api/v1/goals';

/** A goal write moves its tasks too (and a task write moves its goal). */
const ALL = [qk.goals.all(), qk.tasks.all()] as const;

/** Whether the goals module answers at all (404 while switched off). */
export function useGoalsEnabled(signedIn: boolean): boolean {
  const probe = useQuery({
    queryKey: qk.goals.enabled(),
    enabled: signedIn,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) => {
      const { error } = await api.GET(`${BASE}?status=active` as string, { signal });
      return !(error && error.status === 404);
    },
  });
  return probe.data ?? true;
}

export function useGoals(status: 'active' | 'done' | 'dropped', date: string) {
  return useQuery({
    queryKey: qk.goals.list(status, date),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Goal[] }>(BASE, { query: { status, date }, signal })).data,
  });
}

export function useGoal(id: string | null, date: string) {
  return useQuery({
    queryKey: qk.goals.detail(id ?? '', date),
    enabled: id !== null,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: GoalDetail }>(`${BASE}/${id}`, { query: { date }, signal })).data,
  });
}

export function useSaveGoal(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: GoalInput | GoalPatch) =>
      id
        ? (await api.patchJson<{ data: Goal }>(`${BASE}/${id}`, { body })).data
        : (await api.postJson<{ data: Goal }>(BASE, { body })).data,
    { invalidate: ALL, onSuccess },
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
