import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { Task } from '@/lib/tasks';
import type {
  TravelCounter,
  TravelCrossing,
  TravelCrossingInput,
  TravelRule,
  TravelRuleInput,
  TravelSpend,
  TravelTrip,
  TravelTripInput,
  TravelTripItem,
  TravelTripItemInput,
} from '@/lib/travel';

const BASE = '/api/v1/travel';

/** A travel write moves the section and Now's digest (the countdown and the thresholds). */
export const TRAVEL_WRITES = [qk.travel.all(), qk.now.all()] as const;

export function useTravelSettings() {
  return useQuery({
    queryKey: qk.travel.settings(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: { home_country: string | null } }>(`${BASE}/settings`, { signal }))
        .data,
  });
}

export function useSaveTravelSettings(onSuccess?: () => void) {
  return useApiMutation(
    async (home_country: string) =>
      (await api.putJson<{ data: unknown }>(`${BASE}/settings`, { body: { home_country } })).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useCrossings() {
  return useQuery({
    queryKey: qk.travel.crossings(),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TravelCrossing[] }>(`${BASE}/crossings`, {
          query: { limit: 500 },
          signal,
        })
      ).data,
  });
}

export function useSaveCrossing(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TravelCrossingInput) =>
      id
        ? (await api.patchJson<{ data: TravelCrossing }>(`${BASE}/crossings/${id}`, { body })).data
        : (await api.postJson<{ data: TravelCrossing }>(`${BASE}/crossings`, { body })).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteCrossing(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/crossings/${id}`), {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useRules() {
  return useQuery({
    queryKey: qk.travel.rules(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: TravelRule[] }>(`${BASE}/rules`, { signal })).data,
  });
}

export function useSaveRule(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TravelRuleInput) =>
      id
        ? (await api.patchJson<{ data: TravelRule }>(`${BASE}/rules/${id}`, { body })).data
        : (await api.postJson<{ data: TravelRule }>(`${BASE}/rules`, { body })).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useAddRuleTemplate(onSuccess?: () => void) {
  return useApiMutation(
    async (key: 'schengen' | 'kz_tax') =>
      (await api.postJson<{ data: TravelRule }>(`${BASE}/rules/templates/${key}`, {})).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteRule(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/rules/${id}`), {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useCounters(date: string) {
  return useQuery({
    queryKey: qk.travel.counters(date),
    queryFn: async ({ signal }) =>
      await api.getJson<{ data: TravelCounter[]; date: string; current_country: string[] | null }>(
        `${BASE}/counters`,
        { query: { date }, signal },
      ),
  });
}

export function useTrips(currency = 'USD') {
  return useQuery({
    queryKey: qk.travel.trips(currency),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: TravelTrip[] }>(`${BASE}/trips`, { query: { currency }, signal }))
        .data,
  });
}

export function useTrip(id: string, currency = 'USD') {
  return useQuery({
    queryKey: qk.travel.trip(id, currency),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TravelTrip }>(`${BASE}/trips/${id}`, {
          query: { currency },
          signal,
        })
      ).data,
  });
}

export function useSaveTrip(id: string | null, onSuccess?: (trip: TravelTrip) => void) {
  return useApiMutation(
    async (body: TravelTripInput) =>
      id
        ? (await api.patchJson<{ data: TravelTrip }>(`${BASE}/trips/${id}`, { body })).data
        : (await api.postJson<{ data: TravelTrip }>(`${BASE}/trips`, { body })).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteTrip(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/trips/${id}`), {
    invalidate: [...TRAVEL_WRITES, qk.tasks.all(), qk.money.all()],
    onSuccess,
  });
}

export function useSaveItem(tripId: string, id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TravelTripItemInput) =>
      id
        ? (
            await api.patchJson<{ data: TravelTripItem }>(`${BASE}/trips/${tripId}/items/${id}`, {
              body,
            })
          ).data
        : (await api.postJson<{ data: TravelTripItem }>(`${BASE}/trips/${tripId}/items`, { body }))
            .data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteItem(tripId: string, onSuccess?: () => void) {
  return useApiMutation(
    (id: string) => api.deleteJson<unknown>(`${BASE}/trips/${tripId}/items/${id}`),
    {
      invalidate: TRAVEL_WRITES,
      onSuccess,
    },
  );
}

export function useCandidateSpends(tripId: string, enabled: boolean) {
  return useQuery({
    queryKey: qk.travel.candidates(tripId),
    enabled,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TravelSpend[] }>(`${BASE}/trips/${tripId}/candidate-spends`, {
          signal,
        })
      ).data,
  });
}

export function useLinkSpends(tripId: string, onSuccess?: () => void) {
  return useApiMutation(
    async (body: { link?: string[]; unlink?: string[] }) =>
      (
        await api.postJson<{ data: { changed: number } }>(`${BASE}/trips/${tripId}/spends`, {
          body,
        })
      ).data,
    { invalidate: [...TRAVEL_WRITES, qk.money.all()], onSuccess },
  );
}

/** The trip's packing: its tasks. */
export function usePacking(tripId: string) {
  return useQuery({
    queryKey: qk.travel.packing(tripId),
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: Task[] }>('/api/v1/tasks', {
          query: { trip: tripId, limit: 200 },
          signal,
        })
      ).data,
  });
}
