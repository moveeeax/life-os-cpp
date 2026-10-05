import { describe, expect, it } from 'vitest';

import {
  QUEUE_KEY,
  REST_KEY,
  backoffMs,
  describeSet,
  dropFromQueue,
  durationMinutes,
  enqueue,
  exerciseVolume,
  fieldsFor,
  formatClock,
  fromLocalInput,
  healthStatusLabel,
  isPermanentFailure,
  markFailed,
  mergeSets,
  parseSetFields,
  prefill,
  readQueue,
  readRestEnd,
  restRemaining,
  sessionVolume,
  toLocalInput,
  weekdayOf,
  workSetCount,
  writeQueue,
  writeRestEnd,
  type QueuedSet,
} from './session';
import type { WorkoutSessionExercise, WorkoutSet } from './types';

const set = (over: Partial<WorkoutSet> = {}): WorkoutSet => ({
  id: 's1',
  position: 1,
  kind: 'work',
  weight_kg: 100,
  reps: 5,
  duration_seconds: null,
  distance_m: null,
  rpe: null,
  completed_at: '2026-10-05T09:00:00+00:00',
  ...over,
});

const exercise = (over: Partial<WorkoutSessionExercise> = {}): WorkoutSessionExercise => ({
  id: 'e1',
  exercise_id: 'Barbell_Squat',
  position: 1,
  rest_seconds: 90,
  note: '',
  exercise_name: 'Barbell Squat',
  tracking_mode: 'weight_reps',
  images: [],
  primary_muscles: [],
  sets: [],
  previous_sets: [],
  ...over,
});

function memoryStore(initial: Record<string, string> = {}) {
  const data = new Map(Object.entries(initial));
  return {
    getItem: (k: string) => data.get(k) ?? null,
    setItem: (k: string, v: string) => void data.set(k, v),
    removeItem: (k: string) => void data.delete(k),
    data,
  };
}

const queued = (over: Partial<QueuedSet> = {}): QueuedSet => ({
  id: 'q1',
  sessionId: 'sess',
  body: { session_exercise_id: 'e1', kind: 'work', position: 2, weight_kg: 102.5, reps: 5 },
  attempts: 0,
  ...over,
});

describe('time helpers', () => {
  it('numbers weekdays from Monday', () => {
    expect(weekdayOf(new Date(2026, 9, 5))).toBe(1); // 2026-10-05 is a Monday
    expect(weekdayOf(new Date(2026, 9, 11))).toBe(7);
  });

  it('formats a clock with and without hours', () => {
    expect(formatClock(0)).toBe('0:00');
    expect(formatClock(75)).toBe('1:15');
    expect(formatClock(3725)).toBe('1:02:05');
    expect(formatClock(-5)).toBe('0:00');
  });

  it('gives the duration of a finished session only', () => {
    expect(durationMinutes('2026-10-05T09:00:00Z', '2026-10-05T10:02:40Z')).toBe(63);
    expect(durationMinutes('2026-10-05T09:00:00Z', null)).toBeNull();
  });
});

describe('datetime-local conversion', () => {
  it('round-trips an instant to the minute in any time zone', () => {
    const local = toLocalInput('2026-10-05T09:30:45Z');
    expect(local).toMatch(/^2026-10-0[456]T\d{2}:\d{2}$/);
    expect(fromLocalInput(local)).toBe(
      new Date('2026-10-05T09:30:00Z').toISOString().replace('.000Z', 'Z'),
    );
  });

  it('rejects what is not a date', () => {
    expect(fromLocalInput('')).toBeNull();
    expect(fromLocalInput('0002-13-40T99:99')).toBeNull();
    expect(toLocalInput('junk')).toBe('');
  });
});

describe('volume', () => {
  it('sums reps times weight over work sets and skips warm-ups', () => {
    const sets = [
      set(),
      set({ id: 's2', reps: 3 }),
      set({ id: 's3', kind: 'warmup', weight_kg: 50 }),
    ];
    expect(exerciseVolume(sets, 'weight_reps', 80)).toBe(800);
  });

  it('adds body weight for a bodyweight exercise, and only the extra load without one', () => {
    const sets = [set({ weight_kg: 10, reps: 8 }), set({ id: 's2', weight_kg: null, reps: 6 })];
    expect(exerciseVolume(sets, 'bodyweight_reps', 80)).toBe(8 * 90 + 6 * 80);
    expect(exerciseVolume(sets, 'bodyweight_reps', null)).toBe(80);
  });

  it('is zero for timed exercises and sets without reps', () => {
    expect(exerciseVolume([set({ reps: null, duration_seconds: 60 })], 'duration', 80)).toBe(0);
    expect(exerciseVolume([set({ reps: null })], 'weight_reps', 80)).toBe(0);
  });

  it('totals a session and counts its work sets', () => {
    const session = {
      bodyweight_kg: 80,
      exercises: [
        exercise({ sets: [set(), set({ id: 's2', kind: 'warmup' })] }),
        exercise({
          id: 'e2',
          tracking_mode: 'bodyweight_reps',
          sets: [set({ weight_kg: null, reps: 10 })],
        }),
      ],
    };
    expect(sessionVolume(session)).toBe(500 + 800);
    expect(workSetCount(session)).toBe(2);
  });
});

describe('set entry', () => {
  it('knows the fields of each tracking mode', () => {
    expect(fieldsFor('weight_reps')).toEqual(['weight', 'reps']);
    expect(fieldsFor('bodyweight_reps')).toEqual(['weight', 'reps']);
    expect(fieldsFor('duration')).toEqual(['duration']);
    expect(fieldsFor('distance_duration')).toEqual(['distance', 'duration']);
  });

  it('pre-fills the first set from the first work set of the previous session', () => {
    const e = exercise({
      previous_sets: [
        {
          position: 1,
          kind: 'warmup',
          weight_kg: 40,
          reps: 10,
          duration_seconds: null,
          distance_m: null,
          rpe: null,
        },
        {
          position: 2,
          kind: 'work',
          weight_kg: 100,
          reps: 5,
          duration_seconds: null,
          distance_m: null,
          rpe: null,
        },
        {
          position: 3,
          kind: 'work',
          weight_kg: 105,
          reps: 4,
          duration_seconds: null,
          distance_m: null,
          rpe: null,
        },
      ],
    });
    expect(prefill(e)).toMatchObject({ weight: '100', reps: '5' });
    expect(prefill({ ...e, sets: [set()] })).toMatchObject({ weight: '105', reps: '4' });
  });

  it('repeats the set just logged when the previous session had fewer sets', () => {
    const e = exercise({ sets: [set({ weight_kg: 60, reps: 12 })] });
    expect(prefill(e)).toMatchObject({ weight: '60', reps: '12' });
    const before = [
      {
        position: 1,
        kind: 'work' as const,
        weight_kg: 50,
        reps: 10,
        duration_seconds: null,
        distance_m: null,
        rpe: null,
      },
    ];
    expect(prefill({ ...e, previous_sets: before })).toMatchObject({ weight: '60', reps: '12' });
  });

  it('starts empty without history', () => {
    expect(prefill(exercise())).toEqual({ weight: '', reps: '', duration: '', distance: '' });
  });

  it('parses weight with a dot or a comma and needs reps', () => {
    const ok = parseSetFields(
      { weight: '102,5', reps: '5', duration: '', distance: '' },
      'weight_reps',
    );
    expect(ok.errors).toEqual({});
    expect(ok.values).toEqual({
      weight_kg: 102.5,
      reps: 5,
      duration_seconds: null,
      distance_m: null,
    });

    const noReps = parseSetFields(
      { weight: '100', reps: '', duration: '', distance: '' },
      'weight_reps',
    );
    expect(noReps.errors).toEqual({ reps: 'Required' });
  });

  it('lets the extra load of a bodyweight set stay empty', () => {
    const parsed = parseSetFields(
      { weight: '', reps: '8', duration: '', distance: '' },
      'bodyweight_reps',
    );
    expect(parsed.errors).toEqual({});
    expect(parsed.values.weight_kg).toBeNull();
  });

  it('rejects text, negatives, fractions of reps and values over the limits', () => {
    const bad = parseSetFields(
      { weight: '-5', reps: '2.5', duration: '', distance: '' },
      'weight_reps',
    );
    expect(Object.keys(bad.errors).sort()).toEqual(['reps', 'weight']);
    expect(
      parseSetFields({ weight: '2001', reps: '5', duration: '', distance: '' }, 'weight_reps')
        .errors,
    ).toHaveProperty('weight');
    expect(
      parseSetFields({ weight: 'abc', reps: '5', duration: '', distance: '' }, 'weight_reps')
        .errors,
    ).toHaveProperty('weight');
  });

  it('ignores fields of another mode and needs the time of a timed set', () => {
    const timed = parseSetFields(
      { weight: 'junk', reps: 'junk', duration: '60', distance: '' },
      'duration',
    );
    expect(timed.errors).toEqual({});
    expect(timed.values).toEqual({
      weight_kg: null,
      reps: null,
      duration_seconds: 60,
      distance_m: null,
    });
    expect(
      parseSetFields({ weight: '', reps: '', duration: '', distance: '400' }, 'distance_duration')
        .errors,
    ).toEqual({ duration: 'Required' });
  });

  it('describes a set per tracking mode', () => {
    expect(describeSet(set(), 'weight_reps')).toBe('100 kg × 5');
    expect(describeSet(set({ weight_kg: null, reps: 8 }), 'bodyweight_reps')).toBe('BW × 8');
    expect(describeSet(set({ weight_kg: 10, reps: 8 }), 'bodyweight_reps')).toBe('BW + 10 kg × 8');
    expect(describeSet(set({ duration_seconds: 45 }), 'duration')).toBe('45 s');
    expect(describeSet(set({ distance_m: 500, duration_seconds: 120 }), 'distance_duration')).toBe(
      '500 m in 2:00',
    );
  });
});

describe('unsent queue', () => {
  it('survives a reload through the store and clears its key when empty', () => {
    const store = memoryStore();
    writeQueue(store, [queued()]);
    expect(readQueue(store)).toEqual([queued()]);
    writeQueue(store, []);
    expect(store.data.has(QUEUE_KEY)).toBe(false);
  });

  it('reads garbage as an empty queue and drops malformed entries', () => {
    expect(readQueue(memoryStore({ [QUEUE_KEY]: '{not json' }))).toEqual([]);
    expect(readQueue(memoryStore({ [QUEUE_KEY]: '"text"' }))).toEqual([]);
    const mixed = JSON.stringify([queued(), { id: 1 }, null]);
    expect(readQueue(memoryStore({ [QUEUE_KEY]: mixed }))).toEqual([queued()]);
  });

  it('does not throw when the store does', () => {
    const broken = {
      getItem: () => {
        throw new Error('blocked');
      },
      setItem: () => {
        throw new Error('full');
      },
      removeItem: () => {
        throw new Error('blocked');
      },
    };
    expect(readQueue(broken)).toEqual([]);
    expect(() => writeQueue(broken, [queued()])).not.toThrow();
    expect(readRestEnd(broken, 0)).toBeNull();
    expect(() => writeRestEnd(broken, 5)).not.toThrow();
  });

  it('replaces the queued version of the same set and resets its attempts', () => {
    const first = markFailed([queued()], 'q1');
    expect(first[0].attempts).toBe(1);
    const edited = enqueue(first, { ...queued(), body: { ...queued().body, reps: 6 } });
    expect(edited).toHaveLength(1);
    expect(edited[0]).toMatchObject({ attempts: 0, body: { reps: 6 } });
    expect(dropFromQueue(edited, 'q1')).toEqual([]);
  });

  it('backs off 1, 2, 4 … seconds up to 30', () => {
    expect([0, 1, 2, 3, 4, 6, 10].map(backoffMs)).toEqual([
      1000, 1000, 2000, 4000, 8000, 30000, 30000,
    ]);
  });

  it('retries a lost connection, timeouts, an expired session and server errors', () => {
    for (const s of [0, 401, 408, 429, 500, 503])
      expect(isPermanentFailure(s), String(s)).toBe(false);
    for (const s of [400, 403, 404, 409]) expect(isPermanentFailure(s), String(s)).toBe(true);
  });

  it('overlays queued sets on the server sets of their exercise, in order', () => {
    const merged = mergeSets(
      [set(), set({ id: 's3', position: 3 })],
      [
        queued(),
        queued({
          id: 's1',
          body: { session_exercise_id: 'e1', kind: 'work', position: 1, weight_kg: 90, reps: 5 },
          rejected: 'no',
        }),
        queued({ id: 'other', body: { session_exercise_id: 'e2', kind: 'work', position: 1 } }),
      ],
      'e1',
    );
    expect(merged.map((s) => [s.id, s.unsent, s.weight_kg])).toEqual([
      ['s1', true, 90],
      ['q1', true, 102.5],
      ['s3', false, 100],
    ]);
    expect(merged[0].rejected).toBe('no');
    expect(merged[1].kind).toBe('work');
  });
});

describe('rest timer', () => {
  it('counts whole seconds up and stops at zero', () => {
    expect(restRemaining(10_000, 500)).toBe(10);
    expect(restRemaining(10_000, 10_000)).toBe(0);
    expect(restRemaining(10_000, 20_000)).toBe(0);
    expect(restRemaining(null, 0)).toBe(0);
  });

  it('restores a running rest after a reload and forgets a finished one', () => {
    const store = memoryStore();
    writeRestEnd(store, 90_000);
    expect(readRestEnd(store, 30_000)).toBe(90_000);
    expect(readRestEnd(store, 90_000)).toBeNull();
    writeRestEnd(store, null);
    expect(store.data.has(REST_KEY)).toBe(false);
    expect(readRestEnd(memoryStore({ [REST_KEY]: 'junk' }), 0)).toBeNull();
  });
});

describe('healthStatusLabel', () => {
  it('names every status', () => {
    expect(healthStatusLabel('matched')).toBe('Band data attached');
    expect(healthStatusLabel('pending')).toBe('Waiting for band data');
    expect(healthStatusLabel('no_data')).toBe('No band data');
    expect(healthStatusLabel(null)).toBe('In progress');
  });
});
