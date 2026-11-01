import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { NOW_RIBBON, qk } from '@/lib/api/queryKeys';
import type {
  PracticeClient,
  PracticeClientInput,
  PracticeDeal,
  PracticeDealInput,
  PracticeEntry,
  PracticeEntryInput,
  PracticeMonths,
  PracticeMonthView,
} from '@/lib/practice';
import type { Task, TaskInput } from '@/lib/tasks';

const BASE = '/api/v1/practice';

/** A practice write moves every list of the section (a deal's received, a client's income). */
const WRITES = [qk.practice.all()] as const;

export function useClients() {
  return useQuery({
    queryKey: qk.practice.clients(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: PracticeClient[] }>(`${BASE}/clients`, { signal })).data,
  });
}

export function useSaveClient(id: string | null, onSuccess?: (c: PracticeClient) => void) {
  return useApiMutation(
    async (body: PracticeClientInput) =>
      id
        ? (await api.patchJson<{ data: PracticeClient }>(`${BASE}/clients/${id}`, { body })).data
        : (await api.postJson<{ data: PracticeClient }>(`${BASE}/clients`, { body })).data,
    { invalidate: WRITES, onSuccess },
  );
}

export function useDeleteClient(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/clients/${id}`), {
    invalidate: WRITES,
    onSuccess,
  });
}

export function useDeals() {
  return useQuery({
    queryKey: qk.practice.deals(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: PracticeDeal[] }>(`${BASE}/deals`, { signal })).data,
  });
}

export function useSaveDeal(id: string | null, onSuccess?: (d: PracticeDeal) => void) {
  return useApiMutation(
    async (body: PracticeDealInput) =>
      id
        ? (await api.patchJson<{ data: PracticeDeal }>(`${BASE}/deals/${id}`, { body })).data
        : (await api.postJson<{ data: PracticeDeal }>(`${BASE}/deals`, { body })).data,
    { invalidate: WRITES, onSuccess },
  );
}

/** A deleted deal leaves its tasks unlinked: the task lists move too. */
export function useDeleteDeal(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/deals/${id}`), {
    invalidate: [...WRITES, qk.tasks.all()],
    onSuccess,
  });
}

export interface EntryFilter {
  kind?: 'income' | 'expense';
  month?: string;
  client?: string;
  deal?: string;
}

export function useEntries(filter: EntryFilter) {
  const query: Record<string, string | number> = { limit: 200 };
  for (const [k, v] of Object.entries(filter)) if (v) query[k] = v;
  return useQuery({
    queryKey: qk.practice.entries(JSON.stringify(query)),
    queryFn: async ({ signal }) =>
      await api.getJson<{ data: PracticeEntry[]; total: number }>(`${BASE}/entries`, {
        query,
        signal,
      }),
  });
}

export function useSaveEntry(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: PracticeEntryInput) =>
      id
        ? (await api.patchJson<{ data: PracticeEntry }>(`${BASE}/entries/${id}`, { body })).data
        : (await api.postJson<{ data: PracticeEntry }>(`${BASE}/entries`, { body })).data,
    { invalidate: WRITES, onSuccess },
  );
}

export function useDeleteEntry(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/entries/${id}`), {
    invalidate: WRITES,
    onSuccess,
  });
}

export function useMonths(year: number) {
  return useQuery({
    queryKey: qk.practice.months(year),
    queryFn: async ({ signal }) =>
      await api.getJson<PracticeMonths>(`${BASE}/months`, { query: { year }, signal }),
  });
}

export function useMonth(month: string, enabled = true) {
  return useQuery({
    queryKey: qk.practice.month(month),
    enabled,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: PracticeMonthView }>(`${BASE}/months/${month}`, { signal })).data,
  });
}

/** The tasks of one deal (its next steps), open ones first; loaded when the row opens. */
export function useDealTasks(id: string, enabled: boolean) {
  return useQuery({
    queryKey: qk.tasks.byDeal(id),
    enabled,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Task[]; total: number }>('/api/v1/tasks/items', {
          query: { deal: id, limit: 50 },
          signal,
        })
      ).data,
  });
}

/** A next step of a deal is a task with its deal_id. */
export function useAddNextStep(onSuccess?: () => void) {
  return useApiMutation(
    async (body: TaskInput) =>
      (await api.postJson<{ data: Task }>('/api/v1/tasks/items', { body })).data,
    { invalidate: [qk.tasks.all(), ...WRITES, ...NOW_RIBBON], onSuccess },
  );
}
