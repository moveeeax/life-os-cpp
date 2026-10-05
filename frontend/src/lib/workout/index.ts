// Pure logic of the Workout section: no React, no fetch.
import type { Exercise, Routine, RoutineExercise, RoutineInput, TrackingMode } from './types';

/** Primary muscles of the seeded library (yuhonas/free-exercise-db). */
export const MUSCLES = [
  'abdominals',
  'abductors',
  'adductors',
  'biceps',
  'calves',
  'chest',
  'forearms',
  'glutes',
  'hamstrings',
  'lats',
  'lower back',
  'middle back',
  'neck',
  'quadriceps',
  'shoulders',
  'traps',
  'triceps',
] as const;

/** Equipment values of the seeded library. */
export const EQUIPMENT = [
  'bands',
  'barbell',
  'body only',
  'cable',
  'dumbbell',
  'e-z curl bar',
  'exercise ball',
  'foam roll',
  'kettlebells',
  'machine',
  'medicine ball',
  'other',
] as const;

export const TRACKING_MODES: { value: TrackingMode; label: string }[] = [
  { value: 'weight_reps', label: 'Weight and reps' },
  { value: 'bodyweight_reps', label: 'Body weight and reps' },
  { value: 'duration', label: 'Duration' },
  { value: 'distance_duration', label: 'Distance and duration' },
];

export const trackingLabel = (mode: TrackingMode): string =>
  TRACKING_MODES.find((m) => m.value === mode)?.label ?? mode;

/** True when a set of this exercise counts repetitions (not time). */
export const usesReps = (mode: TrackingMode): boolean =>
  mode === 'weight_reps' || mode === 'bodyweight_reps';

/** 1 = Monday … 7 = Sunday, as the API stores it. */
export const WEEKDAYS = [
  'Monday',
  'Tuesday',
  'Wednesday',
  'Thursday',
  'Friday',
  'Saturday',
  'Sunday',
] as const;

export const weekdayLabel = (weekday: number | null | undefined): string =>
  weekday && weekday >= 1 && weekday <= 7 ? WEEKDAYS[weekday - 1] : 'Any day';

/** URL of an exercise photo; `path` is an `images` entry such as `Barbell_Squat/0.jpg`. */
export function mediaUrl(path: string): string {
  return '/exercise-media/' + path.split('/').map(encodeURIComponent).join('/');
}

export const capitalize = (s: string): string => (s ? s[0].toUpperCase() + s.slice(1) : s);

// ── routine editor ──────────────────────────────────────────────────────────

/** One exercise of a routine as the editor holds it: numbers stay text while typed. */
export interface DraftItem {
  /** Stable React key; not sent to the server. */
  key: string;
  exercise_id: string;
  exercise_name: string;
  tracking_mode: TrackingMode;
  images: string[];
  sets: string;
  repsMin: string;
  repsMax: string;
  duration: string;
  rest: string;
  note: string;
}

export interface RoutineDraft {
  name: string;
  weekday: number | null;
  note: string;
  items: DraftItem[];
}

export const emptyDraft = (): RoutineDraft => ({ name: '', weekday: null, note: '', items: [] });

const text = (n: number | null | undefined): string => (n == null ? '' : String(n));

export function itemFromExercise(
  exercise: Pick<Exercise, 'id' | 'name' | 'tracking_mode' | 'images'>,
  key: string,
): DraftItem {
  const reps = usesReps(exercise.tracking_mode);
  return {
    key,
    exercise_id: exercise.id,
    exercise_name: exercise.name,
    tracking_mode: exercise.tracking_mode,
    images: exercise.images,
    sets: '3',
    repsMin: reps ? '8' : '',
    repsMax: reps ? '12' : '',
    duration: '',
    rest: '90',
    note: '',
  };
}

function itemFromRoutine(e: RoutineExercise): DraftItem {
  return {
    key: e.id,
    exercise_id: e.exercise_id,
    exercise_name: e.exercise_name,
    tracking_mode: e.tracking_mode,
    images: e.images,
    sets: text(e.target_sets),
    repsMin: text(e.target_reps_min),
    repsMax: text(e.target_reps_max),
    duration: text(e.target_duration_seconds),
    rest: text(e.rest_seconds),
    note: e.note,
  };
}

export const draftFromRoutine = (r: Routine): RoutineDraft => ({
  name: r.name,
  weekday: r.weekday,
  note: r.note,
  items: r.exercises.map(itemFromRoutine),
});

/**
 * A whole number typed into a field: null for an empty field, NaN for
 * anything that is not a whole number inside [lo, hi].
 */
export function parseField(value: string, lo: number, hi: number): number | null {
  const s = value.trim();
  if (s === '') return null;
  if (!/^\d+$/.test(s)) return NaN;
  const n = Number(s);
  return n >= lo && n <= hi ? n : NaN;
}

// Limits of PUT /api/v1/workout/routines/{id} (docs/openapi.yaml, RoutineInput).
export const LIMITS = {
  name: 120,
  note: 2000,
  exercises: 100,
  sets: [1, 50],
  reps: [1, 1000],
  duration: [1, 86400],
  rest: [0, 3600],
} as const;

/** Field errors of a draft, keyed `name`, `note`, `items` or `<item key>.<field>`. */
export function validateDraft(d: RoutineDraft): Record<string, string> {
  const errors: Record<string, string> = {};
  const name = d.name.trim();
  if (!name) errors.name = 'Give the routine a name.';
  else if (name.length > LIMITS.name) errors.name = `At most ${LIMITS.name} characters.`;
  if (d.note.length > LIMITS.note) errors.note = `At most ${LIMITS.note} characters.`;
  if (d.items.length > LIMITS.exercises) errors.items = `At most ${LIMITS.exercises} exercises.`;

  const range = (key: string, value: string, [lo, hi]: readonly [number, number]) => {
    if (Number.isNaN(parseField(value, lo, hi))) errors[key] = `${lo}–${hi}`;
  };
  for (const item of d.items) {
    range(`${item.key}.sets`, item.sets, LIMITS.sets);
    range(`${item.key}.repsMin`, item.repsMin, LIMITS.reps);
    range(`${item.key}.repsMax`, item.repsMax, LIMITS.reps);
    range(`${item.key}.duration`, item.duration, LIMITS.duration);
    range(`${item.key}.rest`, item.rest, LIMITS.rest);
    const min = parseField(item.repsMin, ...LIMITS.reps);
    const max = parseField(item.repsMax, ...LIMITS.reps);
    if (min !== null && max !== null && min > max) {
      errors[`${item.key}.repsMin`] = 'Above the maximum';
    }
    if (item.note.length > LIMITS.note) errors[`${item.key}.note`] = 'Too long';
  }
  return errors;
}

/** The body of PUT routines/{id}. Call only for a draft without errors. */
export function toRoutineInput(d: RoutineDraft): RoutineInput {
  return {
    name: d.name.trim(),
    weekday: d.weekday,
    note: d.note,
    exercises: d.items.map((item) => {
      const reps = usesReps(item.tracking_mode);
      return {
        exercise_id: item.exercise_id,
        target_sets: parseField(item.sets, ...LIMITS.sets),
        // A field of the other tracking mode is not sent even if it was filled.
        target_reps_min: reps ? parseField(item.repsMin, ...LIMITS.reps) : null,
        target_reps_max: reps ? parseField(item.repsMax, ...LIMITS.reps) : null,
        target_duration_seconds: reps ? null : parseField(item.duration, ...LIMITS.duration),
        rest_seconds: parseField(item.rest, ...LIMITS.rest),
        note: item.note,
      };
    }),
  };
}

/** A copy of `items` with the item at `index` moved by `delta` places; out-of-range moves are ignored. */
export function moveItem<T>(items: readonly T[], index: number, delta: number): T[] {
  const to = index + delta;
  const out = [...items];
  if (index < 0 || index >= out.length || to < 0 || to >= out.length) return out;
  const [item] = out.splice(index, 1);
  out.splice(to, 0, item);
  return out;
}

export function formatSeconds(total: number): string {
  if (total < 60) return `${total} s`;
  const m = Math.floor(total / 60);
  const s = total % 60;
  return s === 0 ? `${m} min` : `${m} min ${s} s`;
}

/** The targets of a routine or session exercise. */
export interface Targets {
  target_sets?: number | null;
  target_reps_min?: number | null;
  target_reps_max?: number | null;
  target_duration_seconds?: number | null;
  rest_seconds: number;
}

/** "4 × 6–10, rest 2 min": the targets of one exercise in a line. */
export function targetSummary(e: Targets): string {
  const { target_reps_min: min, target_reps_max: max, target_duration_seconds: seconds } = e;
  let work = '';
  if (min != null && max != null) work = min === max ? `${min}` : `${min}–${max}`;
  else if (min != null) work = `${min}+`;
  else if (max != null) work = `up to ${max}`;
  else if (seconds != null) work = formatSeconds(seconds);

  const sets = e.target_sets;
  const head = sets != null && work ? `${sets} × ${work}` : sets != null ? `${sets} sets` : work;
  const rest = `rest ${formatSeconds(e.rest_seconds)}`;
  return head ? `${head}, ${rest}` : rest;
}

/** One instruction per non-empty line. */
export const linesOf = (s: string): string[] =>
  s
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean);
