// Row shapes of /api/v1/fitness/* as the API returns them, taken from the
// generated OpenAPI types (the *ListResponse schemas in docs/openapi.yaml,
// which mirror src/repositories/fitness/HealthReadRepository.hpp). SyncRun
// is still spelled out here.

import type { components } from '@/lib/api/schema.gen';

export interface Page<T> {
  data: T[];
  count: number;
  total: number;
}

type Schemas = components['schemas'];
type Row<K extends keyof Schemas> = Schemas[K] extends { data: (infer R)[] } ? R : never;

export type DailyActivity = Row<'DailyActivityListResponse'>;
export type SleepSession = Row<'SleepListResponse'>;
export type SleepStage = SleepSession['stages'][number];
export type HeartRateSample = Row<'HeartRateListResponse'>;
export type StressSample = Row<'StressListResponse'>;
export type Spo2Sample = Row<'Spo2ListResponse'>;
export type BodyMeasurement = Row<'BodyListResponse'>;
export type Workout = Row<'WorkoutListResponse'>;
export type DaySummary = Row<'SummaryListResponse'>;

export type Coverage = Schemas['CoverageResponse']['data'];
export type CoverageEntry = Coverage[string];

export interface SyncRun {
  id: number;
  started_at: string;
  finished_at: string | null;
  status: string; // queued | running | succeeded | failed | interrupted | skipped
  requested_start: string | null;
  requested_end: string | null;
  data_types: string[];
  result: Record<string, unknown> | null;
}

/** Data types the sync accepts (src/fitness/sync/SyncService.hpp, kAllDataTypes). */
export const SYNC_DATA_TYPES = [
  'daily_activity',
  'heart_rate',
  'body_measurements',
  'sleep',
  'workouts',
  'spo2',
  'stress',
  'abnormal_heart_beat',
] as const;
