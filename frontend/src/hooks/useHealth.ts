import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query';

import { api } from '@/lib/api/client';
import { qk } from '@/lib/api/queryKeys';
import { fetchAllPages, widenRange, type DateRange } from '@/lib/health';
import type {
  BodyMeasurement,
  Coverage,
  DailyActivity,
  DaySummary,
  HeartRateSample,
  Page,
  SleepSession,
  Spo2Sample,
  StressSample,
  SyncRun,
  Workout,
} from '@/lib/health/types';

const BASE = '/api/v1/fitness';

/** Every row of a paged fitness route for a date range. */
function fetchRows<T>(route: string, range: DateRange, signal?: AbortSignal): Promise<T[]> {
  // The string cast picks the client's untyped overload: the spec types
  // `data` as plain objects, the row types live in lib/health/types.ts.
  const path: string = `${BASE}/${route}`;
  return fetchAllPages<T>((limit, offset) =>
    api.getJson<Page<T>>(path, {
      query: { from: range.from, to: range.to, limit, offset },
      // Leaving a range cancels its remaining pages.
      signal,
    }),
  );
}

function useRows<T>(route: string, range: DateRange) {
  return useQuery({
    queryKey: qk.health.list(route, range.from, range.to),
    queryFn: ({ signal }) => fetchRows<T>(route, range, signal),
  });
}

// Date-keyed routes take the range as is. Timestamp-keyed routes are asked
// for a day more on each side; the page groups by local day and drops the
// days outside the range.
export const useSummary = (r: DateRange) => useRows<DaySummary>('summary', r);
export const useActivity = (r: DateRange) => useRows<DailyActivity>('daily-activity', r);
export const useSleep = (r: DateRange) => useRows<SleepSession>('sleep', widenRange(r));
export const useHeartRate = (r: DateRange) => useRows<HeartRateSample>('heart-rate', widenRange(r));
export const useStress = (r: DateRange) => useRows<StressSample>('stress', widenRange(r));
export const useSpo2 = (r: DateRange) => useRows<Spo2Sample>('spo2', widenRange(r));
export const useBody = (r: DateRange) => useRows<BodyMeasurement>('body', widenRange(r));
export const useWorkouts = (r: DateRange) => useRows<Workout>('workouts', widenRange(r));

export function useCoverage() {
  const path: string = `${BASE}/coverage`;
  return useQuery({
    queryKey: qk.health.coverage(),
    queryFn: async () => (await api.getJson<{ data: Coverage }>(path)).data,
  });
}

export function isFinalStatus(status: string): boolean {
  return status !== 'queued' && status !== 'running';
}

export function useStartSync() {
  const path: string = `${BASE}/sync`;
  return useMutation({
    mutationFn: async (vars: { from: string; to: string; data_types: string[] }) =>
      (await api.postJson<{ data: { run_id: number; status: string } }>(path, { body: vars })).data,
  });
}

/** How often a running sync is polled. */
export const SYNC_POLL_MS = 3000;

/**
 * Polls a sync run until its status is final, then refreshes the charts.
 * A failed poll stops the polling: the panel shows the error and lets the
 * user stop watching or try again.
 */
export function useSyncRun(id: number | null) {
  const qc = useQueryClient();
  return useQuery({
    queryKey: qk.health.syncRun(id ?? 0),
    enabled: id !== null,
    queryFn: async () => {
      const path: string = `${BASE}/sync/${id}`;
      const run = (await api.getJson<{ data: SyncRun }>(path)).data;
      if (isFinalStatus(run.status)) void qc.invalidateQueries({ queryKey: qk.health.all() });
      return run;
    },
    retry: false,
    refetchInterval: (query) => {
      if (query.state.status === 'error') return false;
      return query.state.data && isFinalStatus(query.state.data.status) ? false : SYNC_POLL_MS;
    },
  });
}

export interface ProbeResult {
  account: string;
  region: string;
  key: string;
  records: number;
}

export function useProbe() {
  const path: string = `${BASE}/probe`;
  return useMutation({
    mutationFn: async (vars: { from: string; to: string }) =>
      (await api.getJson<{ data: ProbeResult }>(path, { query: { key: 'steps', ...vars } })).data,
  });
}
