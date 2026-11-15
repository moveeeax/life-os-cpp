import { useQuery } from '@tanstack/react-query';

import { listQuery, useDeleteEntity, useSaveEntity } from '@/hooks/crud';
import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { NOW_DIGEST, qk } from '@/lib/api/queryKeys';
import type { ResponseOf } from '@/lib/api/types';
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
      (
        await api.getJson<ResponseOf<'/api/v1/travel/settings', 'get'>>(`${BASE}/settings`, {
          signal,
        })
      ).data,
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
  return useSaveEntity<TravelCrossingInput, TravelCrossing>(`${BASE}/crossings`, id, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useDeleteCrossing(onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/crossings`, { invalidate: TRAVEL_WRITES, onSuccess });
}

export function useRules() {
  return useQuery(listQuery<TravelRule>(qk.travel.rules(), `${BASE}/rules`));
}

export function useSaveRule(id: string | null, onSuccess?: () => void) {
  return useSaveEntity<TravelRuleInput, TravelRule>(`${BASE}/rules`, id, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useAddRuleTemplate(onSuccess?: () => void) {
  return useApiMutation(
    async (key: 'schengen' | 'kz_tax') =>
      (await api.postJson<{ data: TravelRule }>(`${BASE}/rules/templates/${key}`, {})).data,
    { invalidate: TRAVEL_WRITES, onSuccess },
  );
}

export function useDeleteRule(onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/rules`, { invalidate: TRAVEL_WRITES, onSuccess });
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
  return useSaveEntity<TravelTripInput, TravelTrip>(`${BASE}/trips`, id, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useDeleteTrip(onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/trips`, {
    invalidate: [...TRAVEL_WRITES, qk.tasks.all(), qk.money.all()],
    onSuccess,
  });
}

export function useSaveItem(tripId: string, id: string | null, onSuccess?: () => void) {
  return useSaveEntity<TravelTripItemInput, TravelTripItem>(`${BASE}/trips/${tripId}/items`, id, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useDeleteItem(tripId: string, onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/trips/${tripId}/items`, { invalidate: TRAVEL_WRITES, onSuccess });
}

export function useSaveContact(tripId: string, id: string | null, onSuccess?: () => void) {
  return useSaveEntity<TravelTripContactInput, TravelTripContact>(
    `${BASE}/trips/${tripId}/contacts`,
    id,
    {
      invalidate: TRAVEL_WRITES,
      onSuccess,
    },
  );
}

export function useDeleteContact(tripId: string, onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/trips/${tripId}/contacts`, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useSaveMoment(tripId: string, id: string | null, onSuccess?: () => void) {
  return useSaveEntity<TravelTripMomentInput, TravelTripMoment>(
    `${BASE}/trips/${tripId}/moments`,
    id,
    {
      invalidate: TRAVEL_WRITES,
      onSuccess,
    },
  );
}

export function useDeleteMoment(tripId: string, onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/trips/${tripId}/moments`, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
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
  return useQuery(listQuery<TravelDocument>(qk.travel.documents(), `${BASE}/documents`));
}

export function useSaveDocument(id: string | null, onSuccess?: () => void) {
  return useSaveEntity<TravelDocumentInput, TravelDocument>(`${BASE}/documents`, id, {
    invalidate: TRAVEL_WRITES,
    onSuccess,
  });
}

export function useDeleteDocument(onSuccess?: () => void) {
  return useDeleteEntity(`${BASE}/documents`, { invalidate: TRAVEL_WRITES, onSuccess });
}
