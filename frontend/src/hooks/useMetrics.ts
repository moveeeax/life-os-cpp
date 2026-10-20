import { useQuery } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import type { MetricEntry } from '@/lib/metrics';

/** The catalog of daily series, expanded for the caller (each currency, category, manual habit). */
export async function fetchMetrics(signal?: AbortSignal): Promise<MetricEntry[]> {
  return (await api.getJson<{ data: MetricEntry[] }>('/api/v1/metrics', { signal })).data;
}

export function useMetrics(enabled = true) {
  return useQuery({
    queryKey: qk.metrics.all(),
    enabled,
    // The catalog moves with a new account, category or habit; a minute is fresh enough.
    staleTime: 60_000,
    queryFn: ({ signal }) => fetchMetrics(signal),
  });
}
