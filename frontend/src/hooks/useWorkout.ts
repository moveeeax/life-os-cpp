import { useInfiniteQuery, useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type {
  Exercise,
  ExerciseInput,
  ExerciseListResponse,
  Routine,
  RoutineInput,
  RoutineSummary,
} from '@/lib/workout/types';

// Paths with an id go through the client's untyped overload (it does not fill
// path templates); the response types come from the same OpenAPI schemas.
const BASE = '/api/v1/workout';

export interface ExerciseFilter {
  q: string;
  muscle: string;
  equipment: string;
  /** '' (all), 'library' or 'custom'. */
  source: string;
}

export const EXERCISE_PAGE = 48;

/** The exercise list for a filter, loaded a page at a time. */
export function useExercises(filter: ExerciseFilter) {
  // Empty filters are not sent: the server reads an empty value as "any".
  const query = Object.fromEntries(Object.entries(filter).filter(([, v]) => v !== ''));
  return useInfiniteQuery({
    queryKey: qk.workout.exercises(query),
    initialPageParam: 0,
    queryFn: ({ pageParam, signal }) =>
      api.getJson<ExerciseListResponse>(`${BASE}/exercises`, {
        query: { ...query, limit: EXERCISE_PAGE, offset: pageParam },
        signal,
      }),
    getNextPageParam: (last, pages) => {
      const loaded = pages.reduce((n, p) => n + p.data.length, 0);
      return last.data.length > 0 && loaded < last.total ? loaded : undefined;
    },
  });
}

export function useCreateExercise(onSuccess: (exercise: Exercise) => void) {
  return useApiMutation(
    async (body: ExerciseInput) =>
      (await api.postJson<{ data: Exercise }>(`${BASE}/exercises`, { body })).data,
    { invalidate: [qk.workout.exercises()], onSuccess },
  );
}

export function useRoutines() {
  return useQuery({
    queryKey: qk.workout.routines(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: RoutineSummary[] }>(`${BASE}/routines`, { signal })).data,
  });
}

export function useRoutine(id: string, enabled: boolean) {
  return useQuery({
    queryKey: qk.workout.routine(id),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: Routine }>(`${BASE}/routines/${id}`, { signal })).data,
    enabled,
    // The editor copies the routine into a draft once; a background refetch
    // must not race the user's edits.
    refetchOnWindowFocus: false,
    staleTime: Infinity,
    gcTime: 0,
  });
}

export function useSaveRoutine(id: string, onSuccess: () => void) {
  return useApiMutation(
    async (body: RoutineInput) =>
      (await api.putJson<{ data: Routine }>(`${BASE}/routines/${id}`, { body })).data,
    { invalidate: [qk.workout.routines(), qk.workout.routine(id)], onSuccess },
  );
}

export function useDeleteRoutine(id: string, onSuccess: () => void) {
  return useApiMutation(() => api.deleteJson<unknown>(`${BASE}/routines/${id}`), {
    invalidate: [qk.workout.routines()],
    onSuccess,
  });
}
