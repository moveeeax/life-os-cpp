// Row shapes of /api/v1/fitness/* as the API returns them. The OpenAPI spec
// types `data` as an array of plain objects, so the fields are spelled out
// here (source: src/repositories/fitness/HealthReadRepository.hpp).

export interface Page<T> {
  data: T[];
  count: number;
  total: number;
}

export interface DailyActivity {
  date: string; // YYYY-MM-DD, the device's local day
  steps: number;
  distance_m: number | null;
  active_kcal: number | null;
  total_kcal: number | null;
  timezone: string;
}

export interface SleepStage {
  stage: string; // deep | light | rem | awake
  minutes: number;
}

export interface SleepSession {
  sleep_id: string;
  start_at: string;
  end_at: string;
  duration_minutes: number;
  time_asleep_minutes: number;
  time_awake_minutes: number;
  sleep_score: number | null;
  sleep_score_source: string | null;
  is_nap: boolean;
  timezone: string;
  stages: SleepStage[];
}

export interface HeartRateSample {
  timestamp: string;
  bpm: number;
  sample_type: string; // passive | active | resting | manual
}

export interface StressSample {
  timestamp: string;
  stress_score: number;
  level: string | null;
}

export interface Spo2Sample {
  timestamp: string;
  spo2_pct: number;
}

export interface BodyMeasurement {
  timestamp: string;
  weight_kg: number;
  bmi: number | null;
  body_fat_pct: number | null;
  muscle_mass_kg: number | null;
  water_pct: number | null;
  bone_mass_kg: number | null;
  visceral_fat_score: number | null;
  basal_metabolism_kcal: number | null;
  metabolic_age: number | null;
}

export interface Workout {
  workout_id: string;
  activity_type: string;
  start_at: string;
  end_at: string | null;
  duration_minutes: number | null;
  distance_m: number | null;
  calories_kcal: number | null;
  avg_heart_rate_bpm: number | null;
  max_heart_rate_bpm: number | null;
  avg_pace_sec_per_km: number | null;
  max_pace_sec_per_km: number | null;
  total_steps: number | null;
}

export interface DaySummary {
  date: string;
  steps: number;
  distance_m: number | null;
  active_kcal: number | null;
  sleep_duration_minutes: number | null;
  sleep_score: number | null;
  resting_bpm: number | null;
}

export interface CoverageEntry {
  first_date: string | null;
  last_date: string | null;
  last_sync_at: string | null;
  records: number;
}
export type Coverage = Record<string, CoverageEntry>;

export interface SyncRun {
  id: number;
  started_at: string;
  finished_at: string | null;
  status: string; // queued | running | succeeded | failed
  requested_start: string;
  requested_end: string;
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
