// Pure logic of a logged workout session: volume, set entry, the queue of
// unsent sets and the rest timer. No React, no fetch; storage is passed in.
import { usesReps } from './index';
import type {
  TrackingMode,
  WorkoutHealthStatus,
  WorkoutPreviousSet,
  WorkoutSession,
  WorkoutSessionExercise,
  WorkoutSet,
  WorkoutSetInput,
} from './types';

// ── time ────────────────────────────────────────────────────────────────────

/** 1 = Monday … 7 = Sunday of a local date, as routines store it. */
export const weekdayOf = (d: Date): number => ((d.getDay() + 6) % 7) + 1;

/** 75 -> "1:15", 3725 -> "1:02:05". */
export function formatClock(totalSeconds: number): string {
  const t = Math.max(0, Math.floor(totalSeconds));
  const h = Math.floor(t / 3600);
  const m = Math.floor((t % 3600) / 60);
  const s = String(t % 60).padStart(2, '0');
  return h > 0 ? `${h}:${String(m).padStart(2, '0')}:${s}` : `${m}:${s}`;
}

/** Whole minutes between two instants; null while the session is not finished. */
export function durationMinutes(startedAt: string, finishedAt: string | null): number | null {
  if (!finishedAt) return null;
  return Math.max(0, Math.round((Date.parse(finishedAt) - Date.parse(startedAt)) / 60000));
}

/** An instant as the value of an <input type="datetime-local">, in local time. */
export function toLocalInput(iso: string): string {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  const p = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}`;
}

/** The instant a datetime-local value names, as ISO 8601 in UTC; null when it is not a date. */
export function fromLocalInput(value: string): string | null {
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}/.test(value)) return null;
  const d = new Date(value);
  return Number.isNaN(d.getTime()) ? null : d.toISOString().replace(/\.\d{3}Z$/, 'Z');
}

// ── volume ──────────────────────────────────────────────────────────────────

type Loaded = Pick<WorkoutSet, 'kind' | 'weight_kg' | 'reps'>;

/**
 * Volume of an exercise: reps times load over its work sets. A bodyweight
 * exercise adds the session's body weight to the set's extra load; without a
 * body weight only the extra load counts. Same rule as the history list.
 */
export function exerciseVolume(
  sets: readonly Loaded[],
  mode: TrackingMode,
  bodyweightKg: number | null,
): number {
  if (!usesReps(mode)) return 0;
  const base = mode === 'bodyweight_reps' ? (bodyweightKg ?? 0) : 0;
  return sets
    .filter((s) => s.kind === 'work' && s.reps != null)
    .reduce((sum, s) => sum + (s.reps ?? 0) * ((s.weight_kg ?? 0) + base), 0);
}

export const sessionVolume = (
  session: Pick<WorkoutSession, 'bodyweight_kg' | 'exercises'>,
): number =>
  session.exercises.reduce(
    (sum, e) => sum + exerciseVolume(e.sets, e.tracking_mode, session.bodyweight_kg),
    0,
  );

export const workSetCount = (session: Pick<WorkoutSession, 'exercises'>): number =>
  session.exercises.reduce((n, e) => n + e.sets.filter((s) => s.kind === 'work').length, 0);

// ── set entry ───────────────────────────────────────────────────────────────

/** What the user types for one set; text until it is sent. */
export interface SetFields {
  weight: string;
  reps: string;
  duration: string;
  distance: string;
}

export type SetField = keyof SetFields;

export const EMPTY_FIELDS: SetFields = { weight: '', reps: '', duration: '', distance: '' };

/** The fields a set of this tracking mode has, in the order they are shown. */
export function fieldsFor(mode: TrackingMode): SetField[] {
  switch (mode) {
    case 'weight_reps':
    case 'bodyweight_reps':
      return ['weight', 'reps'];
    case 'duration':
      return ['duration'];
    case 'distance_duration':
      return ['distance', 'duration'];
  }
}

export const FIELD_LABELS: Record<TrackingMode, Partial<Record<SetField, string>>> = {
  weight_reps: { weight: 'Weight, kg', reps: 'Reps' },
  bodyweight_reps: { weight: 'Extra, kg', reps: 'Reps' },
  duration: { duration: 'Time, s' },
  distance_duration: { distance: 'Distance, m', duration: 'Time, s' },
};

const num = (n: number | null | undefined): string => (n == null ? '' : String(n));

type Values = Pick<WorkoutPreviousSet, 'weight_kg' | 'reps' | 'duration_seconds' | 'distance_m'>;

export const fieldsOf = (s: Values): SetFields => ({
  weight: num(s.weight_kg),
  reps: num(s.reps),
  duration: num(s.duration_seconds),
  distance: num(s.distance_m),
});

/**
 * What the next set starts from: the same-numbered work set of the previous
 * session (so a progression carries over set by set), else the set just
 * logged in this session, else the last set of the previous session.
 */
export function prefill(
  exercise: Pick<WorkoutSessionExercise, 'sets' | 'previous_sets'>,
): SetFields {
  const work = (sets: readonly { kind: string }[]) => sets.filter((s) => s.kind === 'work');
  const done = work(exercise.sets) as WorkoutSet[];
  const before = work(exercise.previous_sets) as WorkoutPreviousSet[];
  const source = before[done.length] ?? done[done.length - 1] ?? before[before.length - 1];
  return source ? fieldsOf(source) : { ...EMPTY_FIELDS };
}

// Limits of PUT /api/v1/workout/sets/{id} (docs/openapi.yaml, WorkoutSetInput).
const SET_LIMITS = {
  weight: 2000,
  reps: 10000,
  duration: 86400,
  distance: 1000000,
} as const;

export interface ParsedSet {
  values: Pick<WorkoutSetInput, 'weight_kg' | 'reps' | 'duration_seconds' | 'distance_m'>;
  /** Per-field messages; empty when the set can be sent. */
  errors: Partial<Record<SetField, string>>;
}

/** A decimal with a dot or a comma (phone keyboards differ); null when empty, NaN when not a number. */
function decimal(text: string): number | null {
  const s = text.trim().replace(',', '.');
  if (s === '') return null;
  return /^\d+(\.\d+)?$/.test(s) ? Number(s) : NaN;
}

function whole(text: string): number | null {
  const s = text.trim();
  if (s === '') return null;
  return /^\d+$/.test(s) ? Number(s) : NaN;
}

/**
 * Reads the typed fields of a set. Weight and distance may stay empty; a rep
 * exercise needs reps and a timed one needs the time, otherwise the set says
 * nothing.
 */
export function parseSetFields(fields: SetFields, mode: TrackingMode): ParsedSet {
  const used = fieldsFor(mode);
  const errors: ParsedSet['errors'] = {};
  const read = (field: SetField, parse: (t: string) => number | null, required: boolean) => {
    if (!used.includes(field)) return null;
    const v = parse(fields[field]);
    if (v === null) {
      if (required) errors[field] = 'Required';
      return null;
    }
    if (Number.isNaN(v) || v > SET_LIMITS[field]) {
      errors[field] = `0–${SET_LIMITS[field]}`;
      return null;
    }
    return v;
  };
  const reps = usesReps(mode);
  return {
    values: {
      weight_kg: read('weight', decimal, false),
      reps: read('reps', whole, reps),
      duration_seconds: read('duration', whole, !reps),
      distance_m: read('distance', decimal, false),
    },
    errors,
  };
}

/** "100 kg × 5", "BW + 10 kg × 8", "45 s", "500 m in 2:00". */
export function describeSet(s: Values, mode: TrackingMode): string {
  const kg = (n: number) => `${Math.round(n * 100) / 100} kg`;
  switch (mode) {
    case 'weight_reps':
      return `${s.weight_kg == null ? '–' : kg(s.weight_kg)} × ${s.reps ?? '–'}`;
    case 'bodyweight_reps':
      return `${s.weight_kg ? `BW + ${kg(s.weight_kg)}` : 'BW'} × ${s.reps ?? '–'}`;
    case 'duration':
      return s.duration_seconds == null ? '–' : `${s.duration_seconds} s`;
    case 'distance_duration': {
      const d = s.distance_m == null ? '–' : `${Math.round(s.distance_m)} m`;
      return s.duration_seconds == null ? d : `${d} in ${formatClock(s.duration_seconds)}`;
    }
  }
}

// ── unsent sets ─────────────────────────────────────────────────────────────

/** A set that has not reached the server yet. Kept in localStorage. */
export interface QueuedSet {
  /** The set's id, generated on the phone: resending replaces, never duplicates. */
  id: string;
  sessionId: string;
  body: WorkoutSetInput;
  /** Failed attempts so far. */
  attempts: number;
  /** Set when the server refused the set for good (4xx); it is not retried. */
  rejected?: string;
}

export const QUEUE_KEY = 'workout.unsentSets';

type Store = Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>;

const isQueued = (x: unknown): x is QueuedSet => {
  if (typeof x !== 'object' || x === null) return false;
  const q = x as Record<string, unknown>;
  return (
    typeof q.id === 'string' &&
    typeof q.sessionId === 'string' &&
    typeof q.body === 'object' &&
    q.body !== null &&
    typeof q.attempts === 'number'
  );
};

/** The stored queue; anything unreadable is treated as an empty queue. */
export function readQueue(store: Store): QueuedSet[] {
  try {
    const parsed: unknown = JSON.parse(store.getItem(QUEUE_KEY) ?? '[]');
    return Array.isArray(parsed) ? parsed.filter(isQueued) : [];
  } catch {
    return [];
  }
}

export function writeQueue(store: Store, queue: readonly QueuedSet[]): void {
  try {
    if (queue.length === 0) store.removeItem(QUEUE_KEY);
    else store.setItem(QUEUE_KEY, JSON.stringify(queue));
  } catch {
    // Storage full or blocked: the queue still lives in memory for this page.
  }
}

/** Adds a set, or replaces the queued version of the same set (an edit before it was sent). */
export function enqueue(
  queue: readonly QueuedSet[],
  item: Omit<QueuedSet, 'attempts'>,
): QueuedSet[] {
  return [...queue.filter((q) => q.id !== item.id), { ...item, attempts: 0 }];
}

export const dropFromQueue = (queue: readonly QueuedSet[], id: string): QueuedSet[] =>
  queue.filter((q) => q.id !== id);

export const markFailed = (
  queue: readonly QueuedSet[],
  id: string,
  rejected?: string,
): QueuedSet[] =>
  queue.map((q) => (q.id === id ? { ...q, attempts: q.attempts + 1, rejected } : q));

/** Delay before the next attempt: 1 s, 2 s, 4 s … capped at 30 s. */
export const backoffMs = (attempts: number): number =>
  Math.min(30000, 1000 * 2 ** Math.max(0, attempts - 1));

/**
 * True when resending cannot help: the server understood the request and
 * refused it. A lost connection (status 0), a timeout, rate limiting, an
 * expired session and server errors are retried.
 */
export const isPermanentFailure = (status: number): boolean =>
  status >= 400 && status < 500 && ![401, 408, 429].includes(status);

/** A set as the page shows it. */
export interface DisplaySet extends WorkoutSet {
  /** Not on the server yet. */
  unsent: boolean;
  rejected?: string;
}

/**
 * The sets of one session exercise as the user sees them: what the server
 * has, overlaid with what is still queued (new sets and unsent edits).
 */
export function mergeSets(
  serverSets: readonly WorkoutSet[],
  queue: readonly QueuedSet[],
  sessionExerciseId: string,
): DisplaySet[] {
  const byId = new Map<string, DisplaySet>(serverSets.map((s) => [s.id, { ...s, unsent: false }]));
  for (const q of queue) {
    if (q.body.session_exercise_id !== sessionExerciseId) continue;
    byId.set(q.id, {
      id: q.id,
      position: q.body.position,
      kind: q.body.kind ?? 'work',
      weight_kg: q.body.weight_kg ?? null,
      reps: q.body.reps ?? null,
      duration_seconds: q.body.duration_seconds ?? null,
      distance_m: q.body.distance_m ?? null,
      rpe: q.body.rpe ?? null,
      completed_at: q.body.completed_at ?? byId.get(q.id)?.completed_at ?? '',
      unsent: true,
      rejected: q.rejected,
    });
  }
  return [...byId.values()].sort(
    (a, b) => a.position - b.position || a.completed_at.localeCompare(b.completed_at),
  );
}

// ── rest timer ──────────────────────────────────────────────────────────────

export const REST_KEY = 'workout.restEndsAt';

/** Whole seconds left until `endsAt` (epoch ms); 0 when over or not running. */
export const restRemaining = (endsAt: number | null, now: number): number =>
  endsAt === null ? 0 : Math.max(0, Math.ceil((endsAt - now) / 1000));

/** The stored end of the rest, or null when none is stored or it is over. */
export function readRestEnd(store: Store, now: number): number | null {
  try {
    const t = Number(store.getItem(REST_KEY));
    return Number.isFinite(t) && t > now ? t : null;
  } catch {
    return null;
  }
}

export function writeRestEnd(store: Store, endsAt: number | null): void {
  try {
    if (endsAt === null) store.removeItem(REST_KEY);
    else store.setItem(REST_KEY, String(endsAt));
  } catch {
    // The timer still runs in the page.
  }
}

// ── health ──────────────────────────────────────────────────────────────────

export function healthStatusLabel(status: WorkoutHealthStatus): string {
  switch (status) {
    case 'matched':
      return 'Band data attached';
    case 'pending':
      return 'Waiting for band data';
    case 'no_data':
      return 'No band data';
    default:
      return 'In progress';
  }
}
