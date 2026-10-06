import { useCallback, useEffect, useRef, useState } from 'react';
import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { ApiClientError, api, apiErrorMessage } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type {
  FoodDay,
  FoodEntry,
  FoodEntryInput,
  FoodEntryPatch,
  FoodGoals,
  FoodGoalsProfile,
  FoodItem,
  FoodItemInput,
  FoodItemListResponse,
  FoodMeal,
  FoodParseJob,
  FoodWeek,
  OffProduct,
} from '@/lib/food/types';

// Paths with an id go through the client's untyped overload (it does not fill
// path templates); the response types come from the same OpenAPI schemas.
const BASE = '/api/v1/food';

/** How often the add form asks whether the parse job has finished. */
export const PARSE_POLL_MS = 2000;

// ── reads ──────────────────────────────────────────────────────────────────

export function useFoodDay(date: string) {
  return useQuery({
    queryKey: qk.food.day(date),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: FoodDay }>(`${BASE}/day`, { query: { date }, signal })).data,
  });
}

export function useFoodWeek(from: string) {
  return useQuery({
    queryKey: qk.food.week(from),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: FoodWeek }>(`${BASE}/week`, { query: { from }, signal })).data,
  });
}

export function useFoodGoals() {
  return useQuery({
    queryKey: qk.food.goals(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: FoodGoals }>(`${BASE}/goals`, { signal })).data,
  });
}

/** Own products matching `q`; archived ones too when asked. */
export function useFoodItems(q: string, archived = false) {
  return useQuery({
    queryKey: qk.food.items(q, archived),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<FoodItemListResponse>(`${BASE}/items`, {
          query: { q: q || undefined, archived: archived ? 'true' : undefined, limit: 200 },
          signal,
        })
      ).data,
  });
}

/** The products logged most recently. */
export function useFoodRecent(enabled = true) {
  return useQuery({
    queryKey: qk.food.recent(),
    enabled,
    queryFn: async ({ signal }) =>
      (await api.getJson<FoodItemListResponse>(`${BASE}/recent`, { signal })).data,
  });
}

/** Open Food Facts search; runs only for a query of two or more characters. */
export function useOffSearch(q: string, enabled: boolean) {
  const query = q.trim();
  return useQuery({
    queryKey: qk.food.offSearch(query),
    enabled: enabled && query.length >= 2,
    staleTime: 60 * 60 * 1000,
    retry: false,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: OffProduct[]; cached: boolean }>(`${BASE}/off/search`, {
          query: { q: query },
          signal,
        })
      ).data,
  });
}

/** Whether the food module answers at all (404 while switched off). */
export function useFoodEnabled(canRead: boolean): boolean {
  const probe = useQuery({
    queryKey: qk.food.enabled(),
    enabled: canRead,
    staleTime: Infinity,
    retry: false,
    queryFn: async ({ signal }) => {
      const { error } = await api.GET(`${BASE}/goals` as string, { signal });
      return !(error && error.status === 404);
    },
  });
  return probe.data ?? true;
}

// ── diary writes ───────────────────────────────────────────────────────────

const DIARY = [qk.food.diary(), qk.food.weeks(), qk.food.recent()] as const;

export function useCreateEntry(onSuccess?: (entry: FoodEntry) => void) {
  return useApiMutation(
    async (body: FoodEntryInput) =>
      (await api.postJson<{ data: FoodEntry }>(`${BASE}/entries`, { body })).data,
    { invalidate: DIARY, onSuccess },
  );
}

export function useCreateEntries(onSuccess?: (entries: FoodEntry[]) => void) {
  return useApiMutation(
    async (entries: FoodEntryInput[]) =>
      (await api.postJson<{ data: FoodEntry[] }>(`${BASE}/entries/batch`, { body: { entries } }))
        .data,
    { invalidate: DIARY, onSuccess },
  );
}

export function useUpdateEntry(id: string, onSuccess?: (entry: FoodEntry) => void) {
  return useApiMutation(
    async (body: FoodEntryPatch) =>
      (await api.patchJson<{ data: FoodEntry }>(`${BASE}/entries/${id}`, { body })).data,
    { invalidate: DIARY, onSuccess },
  );
}

export function useDeleteEntry(id: string, onSuccess?: () => void) {
  return useApiMutation(() => api.deleteJson<unknown>(`${BASE}/entries/${id}`), {
    invalidate: DIARY,
    onSuccess,
  });
}

// ── products ───────────────────────────────────────────────────────────────

const ITEMS = [qk.food.items(), qk.food.recent()] as const;

export function useCreateItem(onSuccess?: (item: FoodItem) => void) {
  return useApiMutation(
    async (body: FoodItemInput) =>
      (await api.postJson<{ data: FoodItem }>(`${BASE}/items`, { body })).data,
    { invalidate: ITEMS, onSuccess },
  );
}

export function useUpdateItem(id: string, onSuccess?: (item: FoodItem) => void) {
  return useApiMutation(
    async (body: FoodItemInput) =>
      (await api.putJson<{ data: FoodItem }>(`${BASE}/items/${id}`, { body })).data,
    // Entries carry the item's name: the diary follows a rename.
    { invalidate: [...ITEMS, ...DIARY], onSuccess },
  );
}

export type ItemDeleteOutcome = 'deleted' | 'archived';

export function useDeleteItem(onSuccess?: (outcome: ItemDeleteOutcome, id: string) => void) {
  return useApiMutation(
    async (id: string) =>
      (await api.deleteJson<{ outcome: ItemDeleteOutcome }>(`${BASE}/items/${id}`)).outcome,
    { invalidate: ITEMS, onSuccess },
  );
}

/** Copy an Open Food Facts product into own products (by barcode). */
export function useItemFromOff(onSuccess?: (item: FoodItem) => void) {
  return useApiMutation(
    async (code: string) =>
      (await api.postJson<{ data: FoodItem }>(`${BASE}/items/from-off`, { body: { code } })).data,
    { invalidate: ITEMS, onSuccess },
  );
}

// ── goals ──────────────────────────────────────────────────────────────────

export function useSaveGoals(onSuccess?: (goals: FoodGoals) => void) {
  return useApiMutation(
    async (body: FoodGoalsProfile) =>
      (await api.putJson<{ data: FoodGoals }>(`${BASE}/goals`, { body })).data,
    { invalidate: [qk.food.goals(), ...DIARY], onSuccess },
  );
}

// ── the parse job ──────────────────────────────────────────────────────────

export type ParseState = 'idle' | 'starting' | 'running' | 'done' | 'failed';

export interface ParseStart {
  text: string;
  meal: FoodMeal;
  date: string;
}

/**
 * One text parse of the add form. `start()` enqueues the job; while `running`
 * the hook polls until the worker finishes. `cancel()` and unmount stop the
 * polling and drop the job's lines: a reopened form starts clean.
 */
export function useParseJob() {
  const [state, setState] = useState<ParseState>('idle');
  const [job, setJob] = useState<FoodParseJob | null>(null);
  const [error, setError] = useState<string | null>(null);
  const starting = useRef(false);
  // Bumped by every start and cancel: an answer of an older job is dropped.
  const generation = useRef(0);

  const start = useCallback(async (body: ParseStart) => {
    if (starting.current) return;
    starting.current = true;
    const mine = ++generation.current;
    setState('starting');
    setError(null);
    setJob(null);
    try {
      const { data } = await api.postJson<{ data: { id: string; status: string } }>(
        `${BASE}/parse`,
        { body },
      );
      if (generation.current !== mine) return;
      setJob({ ...body, id: data.id, status: 'queued' } as FoodParseJob);
      setState('running');
    } catch (e) {
      if (generation.current !== mine) return;
      setError(
        e instanceof ApiClientError && e.code === 'not_configured'
          ? 'Text parsing is not configured on the server.'
          : apiErrorMessage(e),
      );
      setState('failed');
    } finally {
      starting.current = false;
    }
  }, []);

  const cancel = useCallback(() => {
    generation.current++;
    setState('idle');
    setJob(null);
    setError(null);
  }, []);

  const jobId = state === 'running' ? job?.id : undefined;

  useEffect(() => {
    if (!jobId) return;
    const mine = generation.current;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const controller = new AbortController();

    const tick = async () => {
      if (generation.current !== mine) return;
      try {
        const { data } = await api.getJson<{ data: FoodParseJob }>(`${BASE}/parse/${jobId}`, {
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
        // A dropped connection: keep asking.
      }
      timer = setTimeout(() => void tick(), PARSE_POLL_MS);
    };
    void tick();

    return () => {
      controller.abort();
      clearTimeout(timer);
    };
  }, [jobId]);

  // Unmount drops the job: nothing of it survives a closed form.
  useEffect(() => () => void generation.current++, []);

  return { state, job, error, start, cancel };
}
