import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { NOW_DIGEST, qk } from '@/lib/api/queryKeys';
import type { Task } from '@/lib/tasks';
import type {
  TravelCounter,
  TravelCrossing,
  TravelCrossingInput,
  TravelDocument,
  TravelDocumentInput,
  TravelRule,
  TravelRuleInput,
  TravelSpend,
  TravelTrip,
  TravelTripContact,
  TravelTripContactInput,
  TravelTripInput,
  TravelTripItem,
  TravelTripItemInput,
  TravelTripMoment,
  TravelTripMomentInput,
} from '@/lib/travel';
import { todayLocal } from '@/lib/travel';

const BASE = '/api/v1/travel';

/** A travel write moves the section and Now's digest (the countdown and the thresholds). */
export const TRAVEL_WRITES = [qk.travel.all(), ...NOW_DIGEST] as const;

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

export function useCrossings(enabled = true) {
  return useQuery({
    queryKey: qk.travel.crossings(),
    enabled,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TravelCrossing[] }>(`${BASE}/crossings`, {
          query: { limit: 500 },
          signal,
        })
      ).data,
  });
}

/** How many of the newest crossings Now reads to find an arrival (two days back at most). */
export const RECENT_CROSSINGS = 20;

/**
 * The newest crossings only, for Now's arrival card: the API lists them
 * newest first, so a short page finds the last one without the whole history.
 */
export function useRecentCrossings(enabled = true) {
  return useQuery({
    queryKey: [...qk.travel.crossings(), 'recent', RECENT_CROSSINGS] as const,
    enabled,
    staleTime: 10 * 60_000,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TravelCrossing[] }>(`${BASE}/crossings`, {
          query: { limit: RECENT_CROSSINGS },
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

export function useCounters(date: string, enabled = true) {
  return useQuery({
    queryKey: qk.travel.counters(date),
    enabled,
    queryFn: async ({ signal }) =>
      await api.getJson<{ data: TravelCounter[]; date: string; current_country: string[] | null }>(
        `${BASE}/counters`,
        { query: { date }, signal },
      ),
  });
}

export function useTrips(currency = 'USD', enabled = true) {
  return useQuery({
    queryKey: qk.travel.trips(currency),
    enabled,
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: TravelTrip[] }>(`${BASE}/trips`, { query: { currency }, signal }))
        .data,
  });
}

export function useTrip(id: string, currency = 'USD') {
  return useQuery({
    queryKey: qk.travel.trip(id, currency),
    enabled: id !== '',
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TravelTrip }>(`${BASE}/trips/${id}`, {
          // The local day: how long a coming stay may last is counted from it.
          query: { currency, date: todayLocal() },
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

export function useSaveContact(tripId: string, id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TravelTripContactInput) =>
      id
        ? (
            await api.patchJson<{ data: TravelTripContact }>(
              `${BASE}/trips/${tripId}/contacts/${id}`,
              { body },
            )
          ).data
        : (
            await api.postJson<{ data: TravelTripContact }>(`${BASE}/trips/${tripId}/contacts`, {
              body,
            })
          ).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteContact(tripId: string, onSuccess?: () => void) {
  return useApiMutation(
    (id: string) => api.deleteJson<unknown>(`${BASE}/trips/${tripId}/contacts/${id}`),
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useSaveMoment(tripId: string, id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TravelTripMomentInput) =>
      id
        ? (
            await api.patchJson<{ data: TravelTripMoment }>(
              `${BASE}/trips/${tripId}/moments/${id}`,
              { body },
            )
          ).data
        : (
            await api.postJson<{ data: TravelTripMoment }>(`${BASE}/trips/${tripId}/moments`, {
              body,
            })
          ).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteMoment(tripId: string, onSuccess?: () => void) {
  return useApiMutation(
    (id: string) => api.deleteJson<unknown>(`${BASE}/trips/${tripId}/moments/${id}`),
    { invalidate: TRAVEL_WRITES, onSuccess },
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
        await api.getJson<{ data: Task[] }>('/api/v1/tasks/items', {
          query: { trip: tripId, limit: 200 },
          signal,
        })
      ).data,
  });
}

export function useDocuments() {
  return useQuery({
    queryKey: qk.travel.documents(),
    queryFn: async ({ signal }) =>
      (await api.getJson<{ data: TravelDocument[] }>(`${BASE}/documents`, { signal })).data,
  });
}

export function useSaveDocument(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TravelDocumentInput) =>
      id
        ? (await api.patchJson<{ data: TravelDocument }>(`${BASE}/documents/${id}`, { body })).data
        : (await api.postJson<{ data: TravelDocument }>(`${BASE}/documents`, { body })).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteDocument(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/documents/${id}`), {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}
