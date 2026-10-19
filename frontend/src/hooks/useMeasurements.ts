import { useQuery } from '@tanstack/react-query';

import { useApiMutation } from '@/hooks/useApiMutation';
import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { TapeMeasurement, TapeMeasurementInput } from '@/lib/health/measurements';

const BASE = '/api/v1/fitness/measurements';

/** How many measurements the Body block reads: years of weekly ones. */
const LIMIT = 200;

/** The girths measured by hand, newest first. */
export function useMeasurements(enabled = true) {
  return useQuery({
    queryKey: qk.health.measurements(),
    enabled,
    queryFn: async ({ signal }) =>
      (
        await api.getJson<{ data: TapeMeasurement[]; total: number }>(BASE, {
          query: { limit: LIMIT },
          signal,
        })
      ).data,
  });
}

/** Add a measurement, or edit one by id. */
export function useSaveMeasurement(id: string | null, onSuccess?: () => void) {
  return useApiMutation(
    async (body: TapeMeasurementInput) =>
      id
        ? (await api.patchJson<{ data: TapeMeasurement }>(`${BASE}/${id}`, { body })).data
        : (await api.postJson<{ data: TapeMeasurement }>(BASE, { body })).data,
    { invalidate: [qk.health.measurements()], onSuccess },
  );
}

export function useDeleteMeasurement(onSuccess?: () => void) {
  return useApiMutation((id: string) => api.deleteJson<unknown>(`${BASE}/${id}`), {
    invalidate: [qk.health.measurements()],
    onSuccess,
  });
}
